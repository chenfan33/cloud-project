#ifndef STORAGE_H_
#define STORAGE_H_

#include <queue>
#include <regex>
#include <string>

#include "../common/address_parse.h"
#include "../common/kv_interface.h"
#include "../common/proto_gen/proto.pb.h"

namespace storage {
const int kInvalid = -1;
const int kMaxAttempt = 20;

constexpr char kDelim = '\n';
const std::string kMetadataFp = "storage_metadata";

const std::regex kLatestIdRegex = std::regex("Latest\\sID:\\s([0-9]+)");
const std::regex kRootFolderEntryRegex = std::regex(
    "Root "
    "folder:\\s(\\/[a-zA-Z0-9_\\.\\/"
    "\\-\\+\\_=]*)\\sID\\s([0-9]+)\\sItems\\s([0-9]+)");
// Example: folder:/f1/f2/f3/ ID 123
const std::regex kFolderEntryRegex = std::regex(
    "Folder:\\s(\\/[a-zA-Z0-9_\\.\\/"
    "\\-\\+\\_=]*)\\sID\\s([0-9]+)\\sItems\\s([0-9]+)");
// `F` stands for file and `D` stands for dir. Example: file.txt ID 123 F
const std::regex kFolderContentRegex =
    std::regex("([a-zA-Z0-9_\\.\\/\\-\\_\\+=]*)\\sID\\s([0-9]+)\\s([F|D])");

class Dirent {
   public:
    Dirent(const std::string& relative_path, int id, bool is_file)
        : relative_path_(relative_path), id_(id), is_file_(is_file) {}

    std::string Name() { return relative_path_; }
    void UpdateRelativePath(const std::string& relative_path) {
        relative_path_ = relative_path;
    }

    void AddEntry(const std::string& entry_name, Dirent* entry) {
        entries_[entry_name] = entry;
    }
    void DeleteEntry(const std::string& entry_name) {
        entries_.erase(entry_name);
    }

    bool HasEntry(const std::string& entry_name) const {
        return entries_.find(entry_name) != entries_.end();
    }
    Dirent* GetEntry(const std::string& entry_name) {
        return entries_[entry_name];
    }

    void UpdateParent(Dirent* new_parent) { parent_ = new_parent; }

    int ID() const { return id_; }
    std::string Type() const { return is_file_ ? "F" : "D"; }
    bool IsDir() const { return !is_file_; }
    bool IsFile() const { return is_file_; }
    int NumEntries() const { return entries_.size(); }
    void SetIsRoot() { is_root_ = true; }
    bool IsRoot() { return is_root_; }
    Dirent* Parent() { return parent_; }
    
    std::string Path() const {
        if (is_root_) {
            return "/" + relative_path_;
        }

        return parent_ != nullptr ? parent_->Path() + "/" + relative_path_
                                  : relative_path_;
    }
    
    const std::unordered_map<std::string, Dirent*>& Entries() const {
        return entries_;
    }

   private:
    // Complete path of the dir
    std::string relative_path_;
    int id_;
    bool is_file_;
    Dirent* parent_;
    bool is_root_ = false;
    // Map relative path to entries Dirents for faster lookup
    std::unordered_map<std::string, Dirent*> entries_;
};

class StorageHandler {
   public:
    StorageHandler(const std::string& username, int backend_fd)
        : usr_(username), backend_fd_(backend_fd) {}

    StorageServiceResp HandleRequest(const StorageServiceReq& req);
    bool ReadMetadata();
    std::string GetMetadataStr() const;
    std::string GetUser() const { return usr_; }

    // Move to private later after testings are finished.
    bool ValidatePathAndGetDir(std::string path, Dirent** parent_dir,
                               Dirent** dir, std::string& relative_path);

   private:
    //   int GetNextId() { return latest_id_ >= 0 ? ++latest_id_ : kInvalid; }
    void IncrementId() { latest_id_++; }
    void DecrementId() { latest_id_--; }
    int GetLatestId() { return latest_id_; }

    // Handles file upload, directory creation, and directory query (view how
    // many entries there are)
    void HandleCreationAndQuery(const StorageServiceReq& req,
                                StorageServiceResp& resp,
                                int max_attempt = kMaxAttempt);
    void HandleFileDownload(const std::string& fp, StorageServiceResp& resp);
    void HandleDelete(const std::string& fp, StorageServiceResp& resp,
                      int max_attempt = kMaxAttempt,
                      bool expect_kv_failure = false);
    void HandleRename(const FileOrDirRenameReq& rename_req,
                      StorageServiceResp& resp, int max_attempt = kMaxAttempt);
    void HandleMove(const FileOrDirMoveReq& move_req, StorageServiceResp& resp,
                    int max_attempt = kMaxAttempt);
    void HandleQueryAllMovableDir(const std::string& query_all_file_to_move, StorageServiceResp& resp);

    bool ParseMetadataFile();
    Dirent* ParseDirectory(std::stringstream& ss, std::smatch& match,
                           bool is_root = false);
    bool ParseDirEntry(Dirent* parent, const std::string& entry_str);

    bool WriteMetadataToKv(int max_attempt) {
        std::string new_metadata = GetMetadataStr();
        debug_v3(
            "#Storage Service: Writing new metadata for usr %s to file "
            "%s:\n%s\nOld metadata is:\n%s\n",
            usr_.c_str(), kMetadataFp.c_str(), new_metadata.c_str(),
            metadata_str_.c_str());

        if (kv_cput(backend_fd_, usr_, kMetadataFp, metadata_str_,
                    new_metadata) != 0) {
            warn(
                "#Storage Service: CPUT storage metadata failed "
                "at attemp #%d. Reloading state and try again...\n",
                max_attempt);
            return false;
        }

        return true;
    }

    Dirent* FindOrCreateDirMapping(int id, const std::string& entry_name,
                                   bool is_file) {
        // Create Dirent for new folders
        if (directories_.find(id) == directories_.end()) {
            directories_.insert(std::make_pair(
                id, std::make_unique<Dirent>(/*path=*/entry_name, /*id=*/id,
                                             /*is_file=*/is_file)));
        }

        return directories_[id].get();
    }

    void PopulateSuccessResp(StorageServiceResp& resp) {
        resp.set_username(usr_);
        resp.set_status(StorageServiceResp::SUCCESS);
        resp.set_error_msg("");
    }

    bool MaxAttempReached(int max_attempt, StorageServiceResp& resp) {
        if (max_attempt < 0) {
            resp.set_status(StorageServiceResp::FAIL);
            resp.set_error_msg(
                "Creation failed after max attempt due to CPUT failures.");
            return true;
        }
        return false;
    }

    std::string usr_ = "";
    int backend_fd_ = kInvalid;
    // Record the last metadata retrieved from KV store
    std::string metadata_str_ = "";
    // Root folder is named after usr_ and all files for this user will be under
    // the root folder.
    Dirent* root_;
    // Map ID to directory entries
    std::unordered_map<int, std::unique_ptr<Dirent>> directories_;
    // The largest ID being used for mapping files/folders so far
    int latest_id_ = kInvalid;
};



/**************** StorageHandler Implementation ********************/
bool StorageHandler::ReadMetadata() {
    if (kv_gets(backend_fd_, metadata_str_, usr_, kMetadataFp) != 0) {
        warn(
            "#Storage Service: Failed to initialize storage service for "
            "user %s in fd %d\n",
            usr_.c_str(), backend_fd_);
        return false;
    }

    return ParseMetadataFile();
}

StorageServiceResp StorageHandler::HandleRequest(const StorageServiceReq& req) {
    StorageServiceResp resp;
    resp.set_status(StorageServiceResp::FAIL);
    resp.set_error_msg("Invalid request.");

    // Remember to append username
    switch (req.type()) {
        case StorageServiceType::FILE_UPLOAD:
            if (req.has_file_upload_req()) {
                HandleCreationAndQuery(req, resp);
            }
            break;
        case StorageServiceType::FILE_DOWNLOAD:
            if (req.has_file_download_req()) {
                HandleFileDownload(req.file_download_req(), resp);
            }
            break;
        case StorageServiceType::FILE_RENAME:
            // Fall through
        case StorageServiceType::DIR_RENAME:
            if (req.has_rename_req()) {
                HandleRename(req.rename_req(), resp);
            }
            break;
        case StorageServiceType::FILE_DELE:
            if (req.has_delete_req()) {
                HandleDelete(req.delete_req(), resp);
            }
            break;
        case StorageServiceType::DIR_DELE:
            if (req.has_delete_req()) {
                HandleDelete(req.delete_req(), resp);
            }
            break;
        case StorageServiceType::DIR_QUERY:
            // Fall through
        case StorageServiceType::DIR_CREATE:
            if (req.has_dir_create_or_query_req()) {
                HandleCreationAndQuery(req, resp);
            }
            break;
        case StorageServiceType::FILE_MOVE:
            // Fall through
        case StorageServiceType::DIR_MOVE:
            if (req.has_move_req()) {
                HandleMove(req.move_req(), resp);
            }
            break;
        case StorageServiceType::QUERY_ALL_DIR:
            if (req.has_query_all_file_to_move()) {
                HandleQueryAllMovableDir(req.query_all_file_to_move(), resp);
            }
            break;
    }

    return resp;
}

void StorageHandler::HandleMove(const FileOrDirMoveReq& move_req,
                                StorageServiceResp& resp, int max_attempt) {
    if (MaxAttempReached(max_attempt, resp)) {
        return;
    }

    std::string old_path = move_req.path();
    std::string new_dir_path = move_req.new_dir();

    std::string relative_path = "";
    Dirent* cur_entry = nullptr;
    // Old parent of the current entry
    Dirent* old_parent_dir = nullptr;

    std::string new_dir_relative_path = "";
    // Pointer to the new directory
    Dirent* new_dir = nullptr;
    // parent of the new directory
    Dirent* new_dir_parent = nullptr;

    if (!ValidatePathAndGetDir(old_path, &old_parent_dir, &cur_entry,
                               relative_path)) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to find file: " + old_path);
        return;
    }

    if (!ValidatePathAndGetDir(new_dir_path, &new_dir_parent, &new_dir,
                               new_dir_relative_path)) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to locate new directory: " + new_dir_path);
        return;
    }

    std::string new_path = new_dir->Path() + "/" + relative_path;
    // cur_entry->UpdateRelativePath(relative_path);
    cur_entry->UpdateParent(new_dir);

    old_parent_dir->DeleteEntry(relative_path);
    new_dir->AddEntry(relative_path, cur_entry);

    if (!WriteMetadataToKv(max_attempt)) {
        cur_entry->UpdateParent(old_parent_dir);
        old_parent_dir->AddEntry(relative_path, cur_entry);
        new_dir->DeleteEntry(relative_path);

        resp.Clear();
        if (!ReadMetadata()) {
            return;
        }
        HandleMove(move_req, resp, max_attempt - 1);
    } else {
        metadata_str_ = GetMetadataStr();
        PopulateSuccessResp(resp);
    }
}

void StorageHandler::HandleRename(const FileOrDirRenameReq& rename_req,
                                  StorageServiceResp& resp, int max_attempt) {
    if (MaxAttempReached(max_attempt, resp)) {
        return;
    }

    std::string old_path = rename_req.old_path();
    std::string relative_path = "";
    Dirent* cur_dir = nullptr;
    Dirent* parent_dir = nullptr;

    if (!ValidatePathAndGetDir(old_path, &parent_dir, &cur_dir,
                               relative_path)) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to find file: " + old_path);
        return;
    }

    std::string new_path = parent_dir->Path() + "/" + rename_req.new_name();
    cur_dir->UpdateRelativePath(rename_req.new_name());

    parent_dir->DeleteEntry(relative_path);
    parent_dir->AddEntry(rename_req.new_name(), cur_dir);

    if (!WriteMetadataToKv(max_attempt)) {
        cur_dir->UpdateRelativePath(relative_path);
        parent_dir->DeleteEntry(rename_req.new_name());
        parent_dir->AddEntry(relative_path, cur_dir);

        resp.Clear();
        if (!ReadMetadata()) {
            return;
        }
        HandleRename(rename_req, resp, max_attempt - 1);
    } else {
        metadata_str_ = GetMetadataStr();
        PopulateSuccessResp(resp);
    }
}

void StorageHandler::HandleCreationAndQuery(const StorageServiceReq& req,
                                            StorageServiceResp& resp,
                                            int max_attempt) {
    if (MaxAttempReached(max_attempt, resp)) {
        return;
    }

    bool is_file = req.type() == FILE_UPLOAD;
    bool is_query = req.type() == DIR_QUERY;
    std::string path = (is_file ? req.file_upload_req().path()
                                : req.dir_create_or_query_req());

    std::string relative_path = "";
    Dirent* cur_dir = nullptr;
    Dirent* parent_dir = nullptr;
    bool validate_fp =
        ValidatePathAndGetDir(path, &parent_dir, &cur_dir, relative_path);

    // Return error if we are querying folder but path failed to be validated,
    // or if the directory entries are unfound
    if (is_query &&
        (!validate_fp || parent_dir == nullptr || cur_dir == nullptr)) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to find file: " + path);
        warn("#Storage Service: %s\n", resp.error_msg().c_str());
        return;
    }

    // Return error if we are handling file upload and dir creation, but the
    // filepath already exists.
    if (!is_query && validate_fp) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("File or folder already exists: " + path);
        warn("#Storage Service: %s\n", resp.error_msg().c_str());
        return;
    }

    // Return error if we are uploading files or creating directory, and one or
    // more parent directories are unfound.
    if (!is_query && !validate_fp && parent_dir == nullptr) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg(
            "Failed to upload file due to non-existent parent folder(s): " +
            req.file_upload_req().path());
        warn("#Storage Service: %s\n", resp.error_msg().c_str());
        return;
    }

    // If it is creating dir or uploading file
    if (!is_query) {
        // Attempt to obtain the next ID for new file/folder without actually
        // incrementing the latest_id_ field.
        int id = GetLatestId();
        if (id == kInvalid) {
            resp.set_status(StorageServiceResp::FAIL);
            resp.set_error_msg(
                "Failed to upload file due to uninitialized ID: " +
                req.file_upload_req().path());
            warn("#Storage Service: %s\n", resp.error_msg().c_str());
            return;
        }
        id += 1;
        debug(
            "#Storage Service: Get new ID %d when processing req type: "
            "\n%s\n", id, StorageServiceType_Name(req.type()).c_str());

        // Update storage states and tries to write back metadata. If failed,
        // reload state and try for max_attempt times
        directories_[id] = std::make_unique<Dirent>(
            /*relative_path=*/relative_path, /*id=*/id, /*is_file=*/is_file);
        parent_dir->AddEntry(relative_path, directories_[id].get());
        directories_[id]->UpdateParent(parent_dir);

        // Increment ID when request is processed successfully
        IncrementId();
        std::string new_metadata = GetMetadataStr();
        if (!WriteMetadataToKv(max_attempt)) {
            // Revert local changes
            parent_dir->DeleteEntry(relative_path);
            directories_.erase(id);
            DecrementId();

            resp.Clear();
            if (!ReadMetadata()) {
                return;
            }
            // Try again if not exceeding max attempt.
            HandleCreationAndQuery(req, resp, max_attempt - 1);
        } else {
            metadata_str_ = new_metadata;
            if (is_file && kv_puts(backend_fd_, usr_, std::to_string(id),
                                   req.file_upload_req().content()) != 0) {
                if (!ReadMetadata()) {
                    resp.set_status(StorageServiceResp::FAIL);
                    resp.set_error_msg(
                        "Failed to read in updated metadata when reverting "
                        "changes for file upload failures, resulting in "
                        "inconsistent state in metadata and KV store regarding "
                        "file " +
                        path);
                    warn("#Storage Service Create/Upload: %s\n",
                          resp.error_msg().c_str());
                    return;
                }

                // Remove previously metadata changes putted in KV by sending a
                // delete request. We expect the KV delete request in
                // HandleDelete to fail as file was not put in KV.
                HandleDelete(path, resp,
                             /*max_attempt=*/1, /*expect_kv_failure=*/true);
                if (resp.status() != StorageServiceResp::SUCCESS) {
                    warn("#Storage Service: %s\n", resp.error_msg().c_str());
                }

                resp.set_status(StorageServiceResp::FAIL);
                resp.set_error_msg("Failed to upload file " + path +
                                   " to KV with fd " +
                                   std::to_string(backend_fd_));
                warn("#Storage Service: %s\n", resp.error_msg().c_str());
                return;
            }

            // Otherwise success
            PopulateSuccessResp(resp);
        }
    } else {
        auto* dir_info = resp.mutable_dir_info();
        dir_info->set_name(cur_dir->Path());

        for (const auto& [name, entry] : cur_dir->Entries()) {
            auto* add_entry = dir_info->add_entries();
            add_entry->set_name(entry->Path());
            add_entry->set_is_dir(entry->IsDir());
        }
        PopulateSuccessResp(resp);
        debug_v3("#Storage Service: returning query resp %s.\n", resp.DebugString().c_str());
    }
}

void StorageHandler::HandleDelete(const std::string& fp,
                                  StorageServiceResp& resp, int max_attempt,
                                  bool expect_kv_failure) {
    if (MaxAttempReached(max_attempt, resp)) {
        return;
    }

    std::string relative_path = "";
    Dirent* cur_dir = nullptr;
    Dirent* parent_dir = nullptr;

    if (!ValidatePathAndGetDir(fp, &parent_dir, &cur_dir, relative_path)) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to find file " + fp);
        warn("#Storage Service Delete: %s\n", resp.error_msg().c_str());
        return;
    }

    if (cur_dir->IsDir() && cur_dir->NumEntries() > 0) {
        std::vector<std::string> entries;
        for (const auto& [name, entry] : cur_dir->Entries()) {
            std::string entry_fp = entry->Path();
            size_t pos = entry_fp.substr(1, entry_fp.size()).find_first_of('/');
            entry_fp = entry_fp.substr(pos+1, entry_fp.size());
            entries.push_back(entry_fp);
        }

        for (const auto& entry : entries) {
            HandleDelete(entry, resp, /*max_attempt=*/0, expect_kv_failure);
            if (resp.status() == StorageServiceResp_Status_FAIL) {
                warn("%s\n", resp.error_msg().c_str());
                return;
            }
        }
    }
    
    // Delete file
    if (cur_dir->IsFile() &&
        kv_dele(backend_fd_, usr_, std::to_string(cur_dir->ID())) != 0 &&
        !expect_kv_failure) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to delete file " + fp + " to KV with fd " +
                           std::to_string(backend_fd_));

        warn("#Storage Service Delete: %s\n", resp.error_msg().c_str());
        return;
    }

    // Remove from parent dir
    int id = cur_dir->ID();
    parent_dir->DeleteEntry(relative_path);
    std::unique_ptr<Dirent> dir = std::move(directories_[id]);
    directories_.erase(id);

    std::string new_metadata = GetMetadataStr();
    // Revert state if failed to write back metadata
    if (!WriteMetadataToKv(max_attempt)) {
        directories_[id] = std::move(dir);
        parent_dir->AddEntry(relative_path, directories_[id].get());

        resp.Clear();
        if (!ReadMetadata()) {
            resp.set_status(StorageServiceResp::FAIL);
            resp.set_error_msg(
                "Failed to read in updated metadata when reverting "
                "changes for file delete failures, resulting in "
                "inconsistent state in metadata and KV store for file " + fp);
            warn("#Storage Service Delete: %s\n", resp.error_msg().c_str());
            return;
        }
        // Retry and expect KV delete fails as file is already removed from KV.
        HandleDelete(fp, resp, max_attempt - 1, /*expect_kv_failure=*/true);
    } else {
        metadata_str_ = new_metadata;
        PopulateSuccessResp(resp);
    }
}

void StorageHandler::HandleFileDownload(const std::string& fp,
                                        StorageServiceResp& resp) {
    std::string relative_path = "";
    Dirent* cur_dir = nullptr;
    Dirent* parent_dir = nullptr;

    if (!ValidatePathAndGetDir(fp, &parent_dir, &cur_dir, relative_path)) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to find file " + fp);
        return;
    }

    std::string content = "";
    if (kv_gets(backend_fd_, content, usr_, std::to_string(cur_dir->ID())) !=
        0) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to retrieve content for file " + fp);
        return;
    }

    PopulateSuccessResp(resp);
    resp.set_file_download(content);
}

void StorageHandler::HandleQueryAllMovableDir(const std::string& query_all_file_to_move, StorageServiceResp& resp) {
    resp.set_username(usr_);

    if (root_ == nullptr) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Root directory not yet initialized.");
        warn("#Storage Service QueryAllDir: %s\n", resp.error_msg().c_str());
        return;
    }

    std::string relative_path = "";
    Dirent* cur_dir = nullptr;
    Dirent* parent_dir = nullptr;

    if (!ValidatePathAndGetDir(query_all_file_to_move, &parent_dir, &cur_dir, relative_path)) {
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to find file " + query_all_file_to_move);
        warn("#Storage Service QueryAllDir: %s\n", resp.error_msg().c_str());
        return;
    }


    std::queue<Dirent*> queue;
    queue.push(root_);
    auto* movable_dirs = resp.mutable_movable_dirs();

    while(!queue.empty()) {
        Dirent* cur = queue.front();
        queue.pop();
        if (cur_dir->IsDir() && cur_dir == cur) {
            continue;
        }
        if (cur != parent_dir) {
            auto* new_entry = movable_dirs->add_entries();
            new_entry->set_name(cur->Path());
            new_entry->set_is_dir(true);
        }

        for (const auto& [name, dir_entry] : cur->Entries()) {
            if (dir_entry->IsDir()) {
                queue.push(dir_entry);
            }
        }
    }

    resp.set_status(StorageServiceResp::SUCCESS);
}

bool StorageHandler::ValidatePathAndGetDir(std::string path,
                                           Dirent** parent_dir, Dirent** dir,
                                           std::string& relative_path) {
    // If referring to the root folder
    if (path.compare("/") == 0) {
        *parent_dir = root_;
        *dir = root_;
        return true;
    }

    size_t pos = 0;
    std::string token = "";
    *parent_dir = root_;

    while ((pos = path.find('/')) != std::string::npos) {
        token = path.substr(0, pos);
        if (!token.empty()) {
            relative_path = token;
            if (!(*parent_dir)->HasEntry(relative_path)) {
                *parent_dir = nullptr;
                dir = nullptr;
                return false;
            }

            Dirent* temp = (*parent_dir)->GetEntry(relative_path);
            *parent_dir = temp;
        }

        path.erase(0, pos + 1);
    }
    if (!path.empty()) {
        relative_path = path;
        if (!(*parent_dir)->HasEntry(relative_path)) {
            dir = nullptr;
            return false;
        }
    }

    *dir = (*parent_dir)->GetEntry(relative_path);

    return true;
}

bool StorageHandler::ParseMetadataFile() {
    if (metadata_str_.empty()) {
        warn("Metadata for storage service unfound for user %s in fd %d\n",
              usr_.c_str(), backend_fd_);
        return false;
    }

    std::stringstream ss(metadata_str_);
    std::smatch match;
    std::string line;

    while (std::getline(ss, line, kDelim)) {
        if (std::regex_match(line, match, kLatestIdRegex)) {
            latest_id_ = std::stoi(match[1]);
        }

        if (std::regex_match(line, match, kRootFolderEntryRegex)) {
            root_ = ParseDirectory(ss, match, /*is_root=*/true);
            root_->SetIsRoot();
        }

        if (std::regex_match(line, match, kFolderEntryRegex)) {
            ParseDirectory(ss, match);
        }
    }

    return true;
}

Dirent* StorageHandler::ParseDirectory(std::stringstream& ss,
                                       std::smatch& match, bool is_root) {
    std::string dir_fp = match[1];
    std::string relative_path = "";
    auto last = dir_fp.find_last_of('/');
    if (last != std::string::npos) {
        relative_path = dir_fp.substr(last + 1, dir_fp.length());
    }
    int dir_id = std::stoi(match[2]);
    int num_items = std::stoi(match[3]);

    Dirent* dir =
        FindOrCreateDirMapping(dir_id, relative_path, /*is_file=*/false);
    if (is_root) {
        dir->SetIsRoot();
    }

    // Parse directory contents.
    std::string line = "";
    for (int i = 0; i < num_items; i++) {
        if (!std::getline(ss, line, kDelim)) {
            break;
        }

        if (!ParseDirEntry(dir, line)) {
            warn("Failed to parse entry %s for dir %s\n", line.c_str(),
                  dir_fp.c_str());
        }
    }

    return dir;
}

bool StorageHandler::ParseDirEntry(Dirent* parent,
                                   const std::string& entry_str) {
    std::smatch match;
    if (!std::regex_match(entry_str, match, kFolderContentRegex)) {
        return false;
    }

    int id = std::stoi(match[2]);
    bool is_file = match[3].compare("F") == 0 ? true : false;
    std::string entry_relative_name = match[1];
    std::string entry_name = parent->Path() + "/" + entry_relative_name;

    Dirent* entry = FindOrCreateDirMapping(id, entry_relative_name, is_file);
    parent->AddEntry(entry_relative_name, entry);
    entry->UpdateParent(parent);
    return true;
}

std::string StorageHandler::GetMetadataStr() const {
    std::queue<Dirent*> dir_queue;

    std::stringstream ss;

    // Add latest ID
    ss << "Latest ID: " << latest_id_ << "\n";
    // Add root folder information
    ss << "Root folder: " << root_->Path() << " ID " << root_->ID() << " Items "
       << root_->NumEntries() << "\n";

    // Add all entries in root dir;
    for (const auto& [name, entry] : root_->Entries()) {
        if (entry->Type().compare("D") == 0) {
            dir_queue.push(entry);
        }
        ss << name << " ID " << entry->ID() << " " << entry->Type() << "\n";
    }

    while (!dir_queue.empty()) {
        Dirent* cur = dir_queue.front();
        dir_queue.pop();
        ss << "Folder: " << cur->Path() << " ID " << cur->ID() << " Items "
           << cur->NumEntries() << "\n";
        // Add all entries in current dir;
        for (const auto& [name, entry] : cur->Entries()) {
            if (entry->Type().compare("D") == 0) {
                dir_queue.push(entry);
            }
            ss << name << " ID " << entry->ID() << " " << entry->Type() << "\n";
        }
    }

    return ss.str();
}

// Helpers for storage interface

bool InitializeHandler(StorageHandler& handler, StorageServiceResp& resp) {
    if (!handler.ReadMetadata()) {
        warn("Failed to initialize storage service for user %s.",
             handler.GetUser().c_str());
        resp.set_status(StorageServiceResp::FAIL);
        resp.set_error_msg("Failed to initialize storage service for user " +
                           handler.GetUser());
        return false;
    }

    return true;
}

StorageServiceResp create_or_query_dir(int fd, const StorageServiceType& type,
                                       const std::string& usr,
                                       const std::string& complete_fp) {
    StorageHandler handler(/*username=*/usr, /*backend_fd=*/fd);
    StorageServiceResp resp;
    if (!InitializeHandler(handler, resp)) {
        return resp;
    }

    StorageServiceReq req;
    req.set_type(type);
    req.set_username(usr);
    req.set_dir_create_or_query_req(complete_fp);
    resp = handler.HandleRequest(req);

    debug_v3("#Storage Service Interface [Create or Query]: user storage after updates is:\n%s\n",
          handler.GetMetadataStr().c_str());

    return resp;
}

// new_name is the relative path only.
StorageServiceResp rename_file_or_dir(int fd, const StorageServiceType& type,
                                      const std::string& usr,
                                      const std::string& old_complete_fp,
                                      const std::string& new_name) {
    StorageHandler handler(/*username=*/usr, /*backend_fd=*/fd);
    StorageServiceResp resp;
    if (!InitializeHandler(handler, resp)) {
        return resp;
    }

    StorageServiceReq req;
    req.set_type(type);
    req.set_username(usr);
    auto rename_req = req.mutable_rename_req();
    rename_req->set_old_path(old_complete_fp);
    rename_req->set_new_name(new_name);

    resp = handler.HandleRequest(req);
    debug_v3("#Storage Service Interface [Rename]: user storage after updates is:\n%s\n",
          handler.GetMetadataStr().c_str());

    return resp;
}

StorageServiceResp move_file_or_dir(int fd, const StorageServiceType& type,
                                    const std::string& usr,
                                    const std::string& old_complete_fp,
                                    const std::string& new_dir) {
    StorageHandler handler(/*username=*/usr, /*backend_fd=*/fd);
    StorageServiceResp resp;
    if (!InitializeHandler(handler, resp)) {
        return resp;
    }

    StorageServiceReq req;
    req.set_type(type);
    req.set_username(usr);
    auto move_req = req.mutable_move_req();
    move_req->set_path(old_complete_fp);
    move_req->set_new_dir(new_dir);

    resp = handler.HandleRequest(req);
    debug_v3("#Storage Service Interface [Move]: user storage after updates is:\n%s\n",
          handler.GetMetadataStr().c_str());

    return resp;
}

StorageServiceResp delete_file_or_dir(int fd, const StorageServiceType& type,
                                      const std::string& usr,
                                      const std::string& path) {
    StorageHandler handler(/*username=*/usr, /*backend_fd=*/fd);
    StorageServiceResp resp;
    if (!InitializeHandler(handler, resp)) {
        return resp;
    }

    StorageServiceReq req;
    req.set_type(type);
    req.set_username(usr);
    req.set_delete_req(path);

    resp = handler.HandleRequest(req);
    debug_v3("#Storage Service Interface [Delete]: user storage after updates is:\n%s\n",
          handler.GetMetadataStr().c_str());

    return resp;
}

StorageServiceResp upload_file(int fd, const std::string& usr,
                               const std::string& complete_fp,
                               const std::string& content) {
    StorageHandler handler(/*username=*/usr, /*backend_fd=*/fd);
    StorageServiceResp resp;
    if (!storage::InitializeHandler(handler, resp)) {
        return resp;
    }

    StorageServiceReq req;
    req.set_type(StorageServiceType::FILE_UPLOAD);
    req.set_username(usr);
    auto upload_req = req.mutable_file_upload_req();
    upload_req->set_path(complete_fp);
    upload_req->set_content(content);

    resp = handler.HandleRequest(req);
    debug_v3("#Storage Service Interface [Upload]: user storage after updates is:\n%s\n",
          handler.GetMetadataStr().c_str());

    return resp;
}

StorageServiceResp download_file(int fd, const std::string& usr,
                                 const std::string& complete_fp) {
    StorageHandler handler(/*username=*/usr, /*backend_fd=*/fd);
    StorageServiceResp resp;
    if (!storage::InitializeHandler(handler, resp)) {
        return resp;
    }

    StorageServiceReq req;
    req.set_type(StorageServiceType::FILE_DOWNLOAD);
    req.set_username(usr);
    req.set_file_download_req(complete_fp);

    resp = handler.HandleRequest(req);
    debug_v3("#Storage Service Interface [Download]: user storage after updates is:\n%s\n",
          handler.GetMetadataStr().c_str());

    return resp;
}

StorageServiceResp query_movable_dir(int fd, const std::string& usr, const std::string& fp) {
    StorageHandler handler(/*username=*/usr, /*backend_fd=*/fd);
    StorageServiceResp resp;
    if (!storage::InitializeHandler(handler, resp)) {
        return resp;
    }

    StorageServiceReq req;
    req.set_type(StorageServiceType::QUERY_ALL_DIR);
    req.set_username(usr);
    req.set_query_all_file_to_move(fp);

    resp = handler.HandleRequest(req);
    debug_v3("#Storage Service Interface [Query All Dir]: user files should remains the same:\n%s\n",
          handler.GetMetadataStr().c_str());

    return resp;
}

}  // namespace storage

#endif