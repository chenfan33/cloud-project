#ifndef MEMORY_H_
#define MEMORY_H_

#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <regex>
#include <sstream>

#include "chunk.h"
#include "kv_config.h"

namespace KvCache {
namespace fs = ::std::filesystem;
// KvStoreLogEntry Seq 123 user XXX key XXX Op XXX

const std::string kPuts = "Puts";
const std::string kDele = "Dele";
const std::string kDir = "DIR";
const std::string kFile = "FILE";
const std::regex kLoggingHeaderRegex = std::regex(
    "KvStoreLogEntry\\sSeq\\s([0-9]+)\\suser\\s([a-zA-Z0-9\\.\\-\\+\\_=]+)"
    "\\skey\\s([a-"
    "zA-Z0-9\\.\\-\\+\\_=]+)\\sop\\s([a-zA-Z]+)\\slength\\s([0-9]+)\n");
const std::regex kLoggingSequenceIdHeader = std::regex(
    "KvStoreLogEntry\\sCheckpointed\\sat\\sSequenceID:\\s([0-9]+)\n");
const std::regex kFileTransportHeader = std::regex(
    "KvStoreSync\\sFilename:\\s([a-zA-Z0-9_\\.\\/"
    "\\-\\+\\_=]+)\\sType:\\s([a-zA-Z]+)\\ssize:\\s([0-9]+)\r\n");

class KvCache {
   public:
    KvCache() {}

    int Puts(const std::string& user, const std::string& key,
             const std::string& value, int seq_num,
             bool logging_enabled = true);
    int Gets(const std::string& user, const std::string& key,
             std::string& value);
    int GetsAll(const std::string& user, kv_ret& kv_resp);
    int Cputs(const std::string& user, const std::string& key,
              const std::string& prev_value, const std::string& new_value,
              int seq_num);
    int Dele(const std::string& user, const std::string& key, int seq_num,
             bool logging_enabled = true);

    int InitCacheForPrimary();

    // Checkpoint changes in memory
    int Checkpoint();

    // Only invoked when the current node is secondary. Could be called after
    // restart or when first starting up.
    int SecondaryRecoverFromPrimary(int primary_fd);
    int SecondarySendFinishedRecovery(int primary_fd, bool success);

    int PrimarySyncSecondary(int secondary_fd);

    // Replay logging file to sync up memory state
    int ReplayLoggings();

    void UpdateLogging(const std::string& new_logging_filepath) {
        kLogFp_ = new_logging_filepath;
    }

   private:
    // Primary send logging file to secondary for syncing, and secondary
    // determines if full checkpoint is required based on the sequence id.
    int PrimarySendLogging(int secondary_fd);
    // Only invoked when the current node is primary. Failures in reading or
    // sending a single file would effectively terminate the sending process.
    // Note that this could result in inconsistent state between the primary and
    // secondary, and sync failed. It would be the caller (in our case, the
    // backend server) responsibility to decide whether to restart syncing or
    // not.
    int PrimarySendAllContent(int secondary_fd);

    // Secondary checks if requesting full checkpoint from primary is needed for
    // syncing. If full checkpoint is not requested, the loca sequence_id_ will
    // be updated to align with the primary to prepare for logging replay later.
    bool NeedFullSync(const std::string& loggings);
    // Secondary overwrites logging file and replay the logs to recovery
    // in-memory state.
    int OverwriteLoggingAndReplay(const std::string& loggings);
    // Secondary erases all local files and reset sequence_id_ in prepare for
    // full sync.
    void ResetLocalStateForFullSync();
    // Read all files sent from the primary. Terminates and return error if
    // failed to process one of the files. This could result in inconsistent
    // state between the seconary and primary, and it would be the caller (in
    // our case, the backend server) responsibility to decide whether to restart
    // syncing or not.
    int PerformFullSyncFromPrimary(int primary_fd);
    // Primary read local files and write to secondary during syncing.
    int ReadFileAndWriteTo(const std::string& filepath,
                           ssize_t expected_file_size, int fd);
    bool OverwriteFile(const std::string& filepath, const std::string& content);
    bool MatchHeaderAndExtractContent(const std::string& filestr,
                                      std::string& file_path, std::string& type,
                                      int& size, std::string& content);
    bool ExtractCheckpointSequenceIdFromLoggings(const std::string& loggings,
                                                 int& id);

    int Log(const std::string& entry);

    int Puts(const std::string& user, const std::string& key,
             const std::string& value);

    bool ValidateAndUpdateSeqNum(int new_seq);
    std::string FormatPuts(const std::string& user, const std::string& key,
                           const std::string& value, int seq_num);
    std::string FormatDele(const std::string& user, const std::string& key,
                           int seq_num);

    const std::string kOkResp_ = "OK";
    const std::string kRequireFullResp_ = "FULL";
    const std::string kStartSync_ = "SYNC";
    const std::string kSyncDone_ = "SYNC DONE";
    const std::string kSyncError_ = "SYNC ERROR";
    std::string kLogFp_ = PREFIX + "logging";
    // Monotonically increasing ID for serializing operations. If the
    // instruction received has sequence ID not equal to sequence_id_ + 1, then
    // we will either report failures, or wait with a timeout (kTimeout).
    int sequence_id_ = 0;
    // Cache reads. Map users to KV mappings
    std::unordered_map<std::string, KV_Map> read_cache_;
    // Caches recent updates. Map users to KV mappings
    std::unordered_map<std::string, KV_Map> updates_cache_;
};

int KvCache::InitCacheForPrimary() {
    debug_v2("#KvCache: Primary node initializing Kv Cache.\n");
    fs::path logging_file{kLogFp_};

    if (!fs::exists(logging_file)) {
        debug_v2("#KvCache: Primary node creating new logging file.\n");
        std::stringstream ss;
        ss << "KvStoreLogEntry Checkpointed at SequenceID: " << sequence_id_
           << "\n";

        if (!OverwriteFile(kLogFp_, ss.str())) {
            return SYNC_ERROR;
        }

        return FINISHED;
    }

    return ReplayLoggings();
}

int KvCache::Gets(const std::string& user, const std::string& key,
                  std::string& value) {
    if (read_cache_[user].find(key) == read_cache_[user].end()) {
        // If the key is not in the read or updates cache, we need to load it
        // from the KV store.
        if (updates_cache_[user].find(key) == updates_cache_[user].end()) {
            std::string& val = read_cache_[user][key];
            Chunk chunk;
            if (chunk.init(user) != FINISHED) {
                warn("#KvCacheError: Failed to find user %s.\n", user.c_str());
                return USER_ERROR;
            }

            int chunk_ret = chunk.get_value(key, val);
            if (chunk_ret != FINISHED) {
                warn(
                    "#KvCacheError: Failed to retrieve value for key %s, user "
                    "%s.\n",
                    key.c_str(), user.c_str());
                return chunk_ret;
            }

            value = read_cache_[user][key];
            return FINISHED;
        } else {
            value = updates_cache_[user][key];
            return FINISHED;
        }
    }
    value = read_cache_[user][key];
    return FINISHED;
}

int KvCache::GetsAll(const std::string& user, kv_ret& kv_resp) {
    Chunk chunk;
    if (chunk.init(user) != FINISHED) {
        warn("#KvCacheError: Failed to find user %s when getting all.\n",
             user.c_str());
        kv_resp.set_status(USER_ERROR);
        return USER_ERROR;
    }

    KV_Map chunk_kvs = chunk.get_all_kv();

    for (const auto& [key, value] : updates_cache_[user]) {
        if (value != "") {
            chunk_kvs[key] = value;
        } else {
            chunk_kvs.erase(key);
        }
    }

    kv_resp.clear_key_values();
    for (const auto& [key, value] : chunk_kvs) {
        auto* new_kv = kv_resp.add_key_values();
        new_kv->set_key(key);
        new_kv->set_value(value);
    }

    return FINISHED;
}

int KvCache::Puts(const std::string& user, const std::string& key,
                  const std::string& value, int seq_num, bool logging_enabled) {
    if (!ValidateAndUpdateSeqNum(seq_num)) {
        warn(
            "#KvCacheError: Failed to perform Puts operation due to invalid "
            "seq "
            "number %d. "
            "Expecting %d.\n",
            seq_num, sequence_id_ + 1);
        return SEQ_ERROR;
    }

    // Log the operation
    if (logging_enabled) {
        int log_ret = Log(FormatPuts(user, key, value, seq_num));
        if (log_ret != FINISHED) {
            return log_ret;
        }
    }

    return Puts(user, key, value);
}

int KvCache::Cputs(const std::string& user, const std::string& key,
                   const std::string& prev_value, const std::string& new_value,
                   int seq_num) {
    if (!ValidateAndUpdateSeqNum(seq_num)) {
        warn(
            "#KvCacheError: Failed to perform CPuts operation due to invalid "
            "seq "
            "number %d. "
            "Expecting %d.\n",
            seq_num, sequence_id_ + 1);
        return SEQ_ERROR;
    }

    std::string old_value = "";
    int ret = Gets(user, key, old_value);
    if (ret != FINISHED) {
        return ret;
    }

    if (old_value.compare(prev_value) != 0) {
        return VALUE_ERROR;
    }

    int log_ret = Log(FormatPuts(user, key, new_value, seq_num));
    if (log_ret != FINISHED) {
        return log_ret;
    }

    return Puts(user, key, new_value);
}

int KvCache::Dele(const std::string& user, const std::string& key, int seq_num,
                  bool logging_enabled) {
    if (!ValidateAndUpdateSeqNum(seq_num)) {
        warn(
            "KvCacheError: Failed to perform Dele operation due to invalid seq "
            "number %d. "
            "Expecting %d.\n",
            seq_num, sequence_id_ + 1);
        return SEQ_ERROR;
    }

    // Log the operation
    if (logging_enabled) {
        int log_ret = Log(FormatDele(user, key, seq_num));
        if (log_ret != FINISHED) {
            return log_ret;
        }
    }

    return Puts(user, key, "");
}

int KvCache::Checkpoint() {
    debug("#KvCache: Node checkpointing....\n");
    for (const auto& [user, kv_map] : updates_cache_) {
        Chunk chunk;
        chunk.init(user);
        chunk.append_kvs(kv_map);
    }

    // Clear the logging file.
    debug("#KvCache: Ckpt: clearing up logging file\n");
    std::ofstream ofs;
    ofs.open(kLogFp_, std::ofstream::out | std::ofstream::trunc);
    ofs.close();

    // Clear up updates cache and read cache
    debug("#KvCache: Ckpt: clearing up updates and read caches in memory\n");
    updates_cache_.clear();
    read_cache_.clear();

    // Record the sequence id at the time of checkpoint in logging file for
    // recovery and syncing.
    std::stringstream ss;
    ss << "KvStoreLogEntry Checkpointed at SequenceID: " << sequence_id_
       << "\n";

    return Log(ss.str());
}

int KvCache::PrimarySyncSecondary(int secondary_fd) {
    if (!isPrimary) {
        warn(
            "#KvCacheError: node is not primary and cannot perform syncing.\n");
        return SYNC_ERROR;
    }

    debug_v2("#KvCache: Primary start syncing secondary with fd %d.\n",
             secondary_fd);
    int logging_sent = PrimarySendLogging(secondary_fd);
    if (logging_sent != FINISHED) {
        warn(
            "#KvCacheError: primary failed to send logging file to secondary "
            "with fd %d\n",
            secondary_fd);
        return logging_sent;
    }

    std::string secondary_resp = "";
    if (!tcp_read_msg(secondary_fd, secondary_resp)) {
        warn(
            "#KvCacheError: primary failed to read response from secondary "
            "with fd "
            "%d after sending logging file. End syncing.\n",
            secondary_fd);
        return SYNC_ERROR;
    }

    debug_v2(
        "#KvCache: primary received response from secondary with fd %d: %s\n",
        secondary_fd, secondary_resp.c_str());

    if (secondary_resp.compare(kRequireFullResp_) == 0) {
        // Send all content over to secondary
        debug_v2(
            "#KvCache: Primary sending FULL sync to secondary with fd %d.\n",
            secondary_fd);
        int send_all_content = PrimarySendAllContent(secondary_fd);

        std::string primary_sync_all_msg = send_all_content == FINISHED ? kSyncDone_ : kSyncError_;
        if (!tcp_write_msg(secondary_fd, primary_sync_all_msg)) {
            warn(
                "#KvCacheError: primary failed to inform secondary finished sending all files: %d\n",
                secondary_fd);
            return SYNC_ERROR;
        }

        if (send_all_content != FINISHED) {
            warn(
                "#KvCacheError: primary failed to send all files to secondary "
                "with fd %d\n",
                secondary_fd);
            return send_all_content;
        }
    }

    // Wait for final finished msg from secondary
    secondary_resp.clear();
    if (!tcp_read_msg(secondary_fd, secondary_resp)) {
        warn(
            "#KvCacheError: primary failed to read response from secondary "
            "with fd "
            "%d after sending all files. End syncing.\n",
            secondary_fd);
        return SYNC_ERROR;
    }

    debug_v2(
        "#KvCache: Primary received resp for FULL sync from secondary %d: %s\n",
        secondary_fd, secondary_resp.c_str());

    if (secondary_resp.compare(kSyncDone_) == 0) {
        debug("#KvCache: Primary received secondary %d finished syncing msg.\n",
              secondary_fd);
        return FINISHED;
    }

    warn(
        "#KvCacheError: Primart received secondary %d reported syncing "
        "error.\n",
        secondary_fd);
    return SYNC_ERROR;
}

int KvCache::SecondarySendFinishedRecovery(int primary_fd, bool success) {
    if (isPrimary) {
        warn("#KvCacheError: node is primary, \n");
        return SYNC_ERROR;
    }

    std::string sync_done = success ? kSyncDone_ : kSyncError_;

    if (!tcp_write_msg(primary_fd, sync_done)) {
        warn("#KvCacheError: Failed to inform primary %d syncing finished.\n",
             primary_fd);
        return SYNC_ERROR;
    }

    debug_v2("#KvCache-Secondary: Sent syncing finished resp to primary: %s\n",
             sync_done.c_str());
    return FINISHED;
}

// primary sends all files and logging, and need to recover from logging.
int KvCache::SecondaryRecoverFromPrimary(int primary_fd) {
    if (isPrimary) {
        warn(
            "#KvCacheError: node is primary, failed to sync from primary to "
            "primary\n");
        return SYNC_ERROR;
    }

    debug_v2("#KvCache-Secondary: secondary start syncing with primary %d.\n",
             primary_fd);
    kv_command command;
    command.set_com(kStartSync_);
    std::string start_sync_msg = command.SerializeAsString();

    if (!tcp_write_msg(primary_fd, start_sync_msg)) {
        warn(
            "#KvCacheError: Failed to initialize syncing with primary %d. End "
            "syncing.\n",
            primary_fd);
        return SYNC_ERROR;
    }

    debug_v2("#KvCache-Secondary: secondary sent msg to primary %d: %s.\n",
             primary_fd, start_sync_msg.c_str());

    // Expect the first file sending over is the logging file.
    std::string logging_file_msg = "";
    if (!tcp_read_msg(primary_fd, logging_file_msg)) {
        warn(
            "#KvCacheError: Failed to receive logging file when syncing with "
            "primary %d. End syncing.\n",
            primary_fd);
        return SYNC_ERROR;
    }

    debug_v2(
        "#KvCache-Secondary: secondary read loggings from to primary %d.\n",
        primary_fd);
    debug_v3(
        "#KvCache-Secondary: secondary read loggings from to primary %d: %s.\n",
        primary_fd, logging_file_msg.c_str());

    std::string file_path = "";
    std::string type = "";
    std::string loggings = "";
    int file_size = -1;
    if (!MatchHeaderAndExtractContent(logging_file_msg, file_path, type,
                                      file_size, loggings)) {
        warn(
            "#KvCacheError: Failed to read logging file information sent from "
            "primary %d. End syncing\n",
            primary_fd);
        return SYNC_ERROR;
    }

    // Check whether FULL checkpoint syncing is required based on the logging
    // file received, and perform full sync, or only replay, as needed.
    if (NeedFullSync(loggings)) {
        // Send OK response to inform primary no need to send full checkpoint.
        std::string request_full = kRequireFullResp_;
        if (!tcp_write_msg(primary_fd, request_full)) {
            warn(
                "#KvCacheError: Failed to request full checkpoint from primary "
                "%d "
                "during syncing. Syncing failed.\n",
                primary_fd);
            return SYNC_ERROR;
        }

        // Reset local state and prepare for full sync. This includes resetting
        // sequence_id_ to 0, and remove all files (including logging) under
        // PREFIX.
        ResetLocalStateForFullSync();
        // Read from primary and sync up local states (sequence ID and all
        // files)
        return PerformFullSyncFromPrimary(primary_fd);
    } else {
        // Send OK response to inform primary no need to send full checkpoint.
        debug_v2(
            "#KvCache-Secondary: Secondary do not need full sync. Overwrite "
            "and replay loggings.\n");
        std::string ok_resp = kOkResp_;
        if (!tcp_write_msg(primary_fd, ok_resp)) {
            warn(
                "#KvCacheError: Failed to send OK response to primary %d "
                "during syncing. Logging is still replayed.\n",
                primary_fd);
        }

        return OverwriteLoggingAndReplay(loggings);
    }

    debug_v2(
        "#KvCache-Secondary: Syncing finished. Resp not yet sent to "
        "primary.\n");
    return FINISHED;
}

bool KvCache::NeedFullSync(const std::string& loggings) {
    debug_v2("#KvCache-Secondary: Secondary check NeedFullSync.\n");

    int last_checkpoint_id = 0;
    fs::path logging_file{kLogFp_};
    if (fs::exists(logging_file)) {
        debug_v2("#KvCache-Secondary: Secondary parsing local loggings.\n");
        std::string local_loggings = "";
        if (!read_file(kLogFp_, local_loggings)) {
            warn(
                "#KvCacheError: Failed to read local loggings. Requiring full "
                "sync from primary.\n");
            return true;
        } else {
            if (!ExtractCheckpointSequenceIdFromLoggings(local_loggings,
                                                         last_checkpoint_id)) {
                warn(
                    "#KvCacheError: Local logging file - Failed to extract "
                    "sequence ID from logging file "
                    "sent "
                    "from primary, requesting full sync.\n");
                debug_v2(
                    "#KvCacheError: Local logging file - Failed to extract "
                    "sequence ID from logging file "
                    "sent "
                    "from primary, requesting full sync. Logging "
                    "content:\n%s\n",
                    local_loggings.c_str());
                return true;
            }
        }
    }

    debug_v2("#KvCache-Secondary: Secondary local checkpoint ID %d.\n",
             last_checkpoint_id);

    int primary_sequence_id = -1;
    if (!ExtractCheckpointSequenceIdFromLoggings(loggings,
                                                 primary_sequence_id)) {
        warn(
            "#KvCacheError: Failed to extract sequence ID from logging file "
            "sent "
            "from primary, requesting full sync.\n");
        debug_v3(
            "#KvCacheError: Failed to extract sequence ID from logging file "
            "sent "
            "from primary, requesting full sync. Logging content:\n%s\n",
            loggings.c_str());
        return true;
    }

    debug(
        "#KvCache-Secondary: Primary has sequence id %d, local checkpoint has "
        "sequence id is %d\n",
        primary_sequence_id, last_checkpoint_id);
    // Requires full sync if the sequence id recorded in checkpoint is more
    // advanced than node's local sequence id.
    if (primary_sequence_id > last_checkpoint_id) {
        debug("#KvCache-Secondary: Requesting full sync from primary\n");
        return true;
    }

    // If primary sequence id is equal to or less than the local ID, the primary
    // has not checkpointed since the node crash, no need to request full sync.
    // Update the sequence_id_ to align with primary, to prepare for replaying
    // logging file later.
    debug_v2("#KvCache-Secondary: Secondary DO NOT require full sync.\n");
    return false;
}

void KvCache::ResetLocalStateForFullSync() {
    sequence_id_ = 0;
    max_sequence = 0;
    updates_cache_.clear();
    read_cache_.clear();

    fs::path dir{PREFIX};
    fs::remove_all(dir);
    debug_v2(
        "#KvCache-Secondary: Cleared up all cache and removed all local "
        "storages for FULL SYNC.\n");
}

int KvCache::PerformFullSyncFromPrimary(int primary_fd) {
    debug_v2("#KvCache-Secondary: Starting full sync from primary %d.\n",
             primary_fd);

    std::string msg_from_primary = "";
    bool received_all_files = false;

    while (tcp_read_msg(primary_fd, msg_from_primary)) {
        debug_v3("#KvCache-Secondary: Secondary received msg from primary during full sync: %s.\n",
             msg_from_primary.c_str());
        if (msg_from_primary.compare(kSyncDone_) == 0) {
            received_all_files = true;
            break;
        }

        if (msg_from_primary.compare(kSyncError_) == 0) {
            received_all_files = false;
            break;
        }

        std::string file_path = "";
        std::string type = "";
        std::string file_content = "";
        int file_size = -1;
        if (!MatchHeaderAndExtractContent(msg_from_primary, file_path, type,
                                          file_size, file_content)) {
            warn(
                "#KvCacheError: Failed to read file information sent from "
                "primary %d. End syncing\n",
                primary_fd);
            return SYNC_ERROR;
        }

        file_path = PREFIX + file_path;

        if (type.compare(kDir) == 0) {
            debug_v2(
                "#KvCache-Secondary-fullsync: received directory from primary "
                "%d: %s\n",
                primary_fd, file_path.c_str());
            // Create if it is a directory
            if (!fs::create_directories(file_path)) {
                warn(
                    "#KvCacheError: Failed to create directory %s. End "
                    "syncing\n",
                    file_path.c_str());
                return SYNC_ERROR;
            }
        } else if (type.compare(kFile) == 0) {
            // If it is a file
            debug_v2(
                "#KvCache-Secondary-fullsync: received file from primary %d: "
                "%s\n",
                primary_fd, file_path.c_str());
            fs::path filepath{file_path};
            std::error_code code;
            if (!fs::create_directories(filepath.parent_path(), code)) {
                if (code.message().compare("Success") != 0) {
                    warn(
                        "#KvCacheError: Failed to create directory %s for "
                        "writing "
                        "file %s due to %s and %d. End syncing\n",
                        filepath.parent_path().c_str(), file_path.c_str(),
                        code.message().c_str(), code.value());
                    return SYNC_ERROR;
                }
            }

            if (!OverwriteFile(filepath, file_content)) {
                warn(
                    "#KvCacheError: Failed to overwrite file %s. End syncing\n",
                    file_path.c_str());
                return SYNC_ERROR;
            }
        } else {
            // LOG unknown type
            warn("#KvCacheError: Unknown dirent type %s during syncing.\n",
                 type.c_str());
            return SYNC_ERROR;
        }

        msg_from_primary.clear();
    }

    // After all files are read from primary and write to local, replay the
    // loggings to sync the memory state.
    if (received_all_files) {
        return ReplayLoggings();
    } else {
        warn("#KvCacheError: Secondary failed to receive all files during full sync. Existing...\n");
        return SYNC_ERROR;
    }

}

int KvCache::PrimarySendLogging(int secondary_fd) {
    // Validate that the kv dire at PREFIX exists.
    fs::path logging_file{kLogFp_};
    if (!fs::exists(logging_file)) {
        error("#KvCache: Failed to identify logging file %s on node.\n",
              kLogFp_.c_str());
        return SYNC_ERROR;
    }

    debug_v2("#KvCache: Primary sending loggings to secondary with fd %d.\n",
             secondary_fd);
    return ReadFileAndWriteTo(kLogFp_, fs::file_size(logging_file),
                              secondary_fd);
}

int KvCache::PrimarySendAllContent(int secondary_fd) {
    // Validate that the kv dire at PREFIX exists.
    fs::path kv_dir{PREFIX};
    if (!fs::exists(kv_dir)) {
        error("#KvCacheError: Failed to identify storage dir %s on node.\n",
              PREFIX.c_str());
        return SYNC_ERROR;
    }

    // Iterate through all files under the directory and send them over to the
    // secondary
    for (const fs::directory_entry& dirent :
         fs::recursive_directory_iterator(PREFIX)) {
        // Skip directories, as we will send the complete path to secondary,
        // directory info is already included.
        std::string fp = dirent.path().string();
        fp = fp.substr(PREFIX.length(), fp.length());

        if (dirent.is_directory()) {
            std::stringstream ss;
            ss << "KvStoreSync Filename: " << fp << " Type: " << kDir
               << " size: " << 0 << "\r\n";
            std::string msg = ss.str();
            tcp_write_msg(secondary_fd, msg);
            debug_v2("#KvCache: Primary sent directory to %d: %s\n",
                     secondary_fd, fp.c_str());
            continue;
        }

        int write_file =
            ReadFileAndWriteTo(dirent.path(), dirent.file_size(), secondary_fd);
        if (write_file != FINISHED) {
            warn(
                "#KvCacheError: Failed to read and send file %s to secondary "
                "%d.\n",
                dirent.path().c_str(), secondary_fd);
            return write_file;
        }
    }

    return FINISHED;
}

int KvCache::OverwriteLoggingAndReplay(const std::string& loggings) {
    debug_v2("#KvCache-Secondary: Secondary overwrite logging.\n");
    if (!OverwriteFile(kLogFp_, loggings)) {
        warn(
            "#KvCacheError: Failed to overwrite log file during syncing. Sync "
            "failed.\n");
        return SYNC_ERROR;
    }

    return ReplayLoggings();
}

int KvCache::ReplayLoggings() {
    std::string loggings;
    if (!read_file(kLogFp_, loggings)) {
        warn(
            "#KvCacheError: Failed to open logging file for syncing. Recovery "
            "FAILED, this could lead to inconsistent state.\n");
        return REC_ERROR;
    }

    if (!ExtractCheckpointSequenceIdFromLoggings(loggings, sequence_id_)) {
        warn(
            "#KvCacheError: Failed to extract last checkpoint sequence ID "
            "during replying. Recovery "
            "FAILED, this could lead to inconsistent state.\n");
        return REC_ERROR;
    }

    debug_v2(
        "#KvCache-Replay: Cleared up memory state and start replying "
        "loggings with sequence_id_ %d\n", sequence_id_);
    // Clear up the caches before replaying.
    updates_cache_.clear();
    read_cache_.clear();

    int seq_num = 0;
    std::smatch match;
    while (std::regex_search(loggings, match, kLoggingHeaderRegex)) {
        int seq_num = std::stoi(match[1]);
        std::string user = match[2];
        std::string key = match[3];
        std::string op_type = match[4];
        int length = std::stoi(match[5]);
        int new_str_start = match.position() + match.length();

        if (op_type.compare(kPuts) == 0) {
            // Replay puts
            std::string value = loggings.substr(new_str_start, length);
            // Plus one for the newline char
            new_str_start += value.size() + 1;

            if (Puts(user, key, value, seq_num, /*logging_enabled=*/false) !=
                FINISHED) {
                warn(
                    "#KvCacheError: Replay FAILED for PUTS request for user %s "
                    "and "
                    "key %s.\n",
                    user.c_str(), key.c_str());
                return REC_ERROR;
            }

        } else if (op_type.compare(kDele) == 0) {
            if (Dele(user, key, seq_num, /*logging_enabled=*/false) !=
                FINISHED) {
                warn(
                    "#KvCacheError: Replay FAILED for Dele request for user %s "
                    "and "
                    "key %s.\n",
                    user.c_str(), key.c_str());
                return REC_ERROR;
            }

        } else {
            warn("#KvCacheError: Invalid operation %s when replaying.\n",
                 op_type.c_str());
        }

        loggings = loggings.substr(new_str_start, loggings.length());
    }

    max_sequence = sequence_id_;
    debug_v2(
        "#KvCache-Replay: Max sequence updated to align with sequence_id_: "
        "%d\n",
        max_sequence);
    return FINISHED;
}

int KvCache::Log(const std::string& entry) {
    std::fstream fs;

    fs.open(kLogFp_, std::ios::out | std::ios::app | std::ios::binary);
    if (!fs.is_open()) {
        warn(
            "#KvCacheError: Failed to open loging file for recording entry. "
            "Entry %s is "
            "not "
            "logged.\n",
            entry.c_str());
        return LOG_ERROR;
    }

    debug_v3("#KvCache: writing entry to logging: %s\n", entry.c_str());
    fs << entry;
    fs.close();
    return FINISHED;
}

int KvCache::Puts(const std::string& user, const std::string& key,
                  const std::string& value) {
    if (updates_cache_.find(user) == updates_cache_.end()) {
        // Load from KV and update
        updates_cache_[user] = {};
    }
    updates_cache_[user][key] = value;

    // Update read cache if the updated KV pair was in the read-only cache as
    // well.
    if (read_cache_.find(user) != updates_cache_.end()) {
        auto& user_map = read_cache_[user];
        if (user_map.find(key) != user_map.end()) {
            user_map.erase(key);
        }

        if (user_map.empty()) {
            read_cache_.erase(user);
        }
    }

    return FINISHED;
}

bool KvCache::ValidateAndUpdateSeqNum(int new_seq) {
    if (new_seq != sequence_id_ + 1) {
        return false;
    }

    sequence_id_ = new_seq;
    return true;
}

std::string KvCache::FormatPuts(const std::string& user, const std::string& key,
                                const std::string& value, int seq_num) {
    std::stringstream ss;
    ss << "KvStoreLogEntry Seq " << seq_num << " user " << user << " key "
       << key << " op " << kPuts << " length " << value.size() << "\n"
       << value << "\n";
    return ss.str();
}

std::string KvCache::FormatDele(const std::string& user, const std::string& key,
                                int seq_num) {
    std::stringstream ss;
    ss << "KvStoreLogEntry Seq " << seq_num << " user " << user << " key "
       << key << " op " << kDele << " length 0"
       << "\n";
    return ss.str();
}

bool KvCache::OverwriteFile(const std::string& filepath,
                            const std::string& content) {
    debug_v2("#KvCache: Overwriting file %s\n", filepath.c_str());
    std::fstream fs;
    fs.open(filepath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!fs.is_open()) {
        warn("#KvCacheError: Failed to overwrite file %s.\n", filepath.c_str());
        return false;
    }

    fs << content;
    fs.close();

    return true;
}

int KvCache::ReadFileAndWriteTo(const std::string& filepath,
                                ssize_t expected_file_size, int fd) {
    std::string content;
    if (!read_file(filepath, content)) {
        warn(
            "#KvCacheError: Failed to read file %s during syncing. File "
            "content "
            "not transmitted to secondary.\n",
            filepath.c_str());
        return SYNC_ERROR;
    }

    if (content.size() != expected_file_size) {
        warn(
            "#KvCacheError: File size differs for file %s during read while "
            "syncing. Read %ld bytes but size is %ld. Writing actual read "
            "size to secondary.\n",
            filepath.c_str(), content.size(), expected_file_size);
    }

    std::stringstream ss;
    std::string sent_fp = filepath.substr(PREFIX.length(), filepath.length());
    ss << "KvStoreSync Filename: " << sent_fp << " Type: " << kFile
       << " size: " << content.size() << "\r\n";
    std::string msg = ss.str() + content;
    tcp_write_msg(fd, msg);

    debug_v2("#KvCache: Primary sent file to with fd %d: %s\n", fd,
             sent_fp.c_str());

    return FINISHED;
}

bool KvCache::MatchHeaderAndExtractContent(const std::string& filestr,
                                           std::string& file_path,
                                           std::string& type, int& size,
                                           std::string& content) {
    std::smatch match;
    if (!std::regex_search(filestr, match, kFileTransportHeader)) {
        warn("#KvCacheError: Failed to locate file transport header %s.\n",
             filestr.c_str());
        return false;
    }

    file_path = match[1];
    type = match[2];
    size = std::stoi(match[3]);
    int new_str_start = match.position() + match.length();
    content = filestr.substr(new_str_start, filestr.length());
    return true;
}

bool KvCache::ExtractCheckpointSequenceIdFromLoggings(
    const std::string& loggings, int& id) {
    std::smatch match;
    if (!std::regex_search(loggings, match, kLoggingSequenceIdHeader)) {
        warn(
            "#KvCacheError: Failed to extract sequence ID from logging file "
            "content.\n");
        debug_v3(
            "#KvCacheError: Failed to extract sequence ID from logging file "
            "content. Logging content:\n%s\n",
            loggings.c_str());
        return false;
    }

    id = std::stoi(match[1]);
    return true;
}

}  // namespace KvCache

#endif