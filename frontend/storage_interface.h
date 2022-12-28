#ifndef STORAGE_INTERFACE_H_
#define STORAGE_INTERFACE_H_

#include "storage.h"

namespace {
using StorageHandler = storage::StorageHandler;
using Dirent = storage::Dirent;

}  // namespace

StorageServiceResp query_rootdir(int fd, const std::string& usr) {
    return storage::create_or_query_dir(fd, StorageServiceType::DIR_QUERY, usr,
                                        "/");
}

StorageServiceResp upload_file(int fd, const std::string& usr,
                               const std::string& complete_fp,
                               const std::string& content) {
    return storage::upload_file(fd, usr, complete_fp, content);
}

StorageServiceResp download_file(int fd, const std::string& usr,
                                 const std::string& complete_fp) {
    return storage::download_file(fd, usr, complete_fp);
}
// RENAME
// new_name is the relative path only.
StorageServiceResp rename_file(int fd, const std::string& usr,
                               const std::string& old_complete_fp,
                               const std::string& new_name) {
    return storage::rename_file_or_dir(fd, StorageServiceType::FILE_RENAME, usr,
                                       old_complete_fp, new_name);
}
// new_name is the relative path only.
StorageServiceResp rename_dir(int fd, const std::string& usr,
                              const std::string& old_complete_fp,
                              const std::string& new_name) {
    return storage::rename_file_or_dir(fd, StorageServiceType::DIR_RENAME, usr,
                                       old_complete_fp, new_name);
}

// MOVE
// new_name is the relative path only.
StorageServiceResp move_file(int fd, const std::string& usr,
                             const std::string& old_complete_fp,
                             const std::string& new_dir_name) {
    return storage::move_file_or_dir(fd, StorageServiceType::FILE_MOVE, usr,
                                     old_complete_fp, new_dir_name);
}
StorageServiceResp move_dir(int fd, const std::string& usr,
                            const std::string& old_complete_fp,
                            const std::string& new_dir_name) {
    return storage::move_file_or_dir(fd, StorageServiceType::DIR_MOVE, usr,
                                     old_complete_fp, new_dir_name);
}

// DELETE
StorageServiceResp delete_file(int fd, const std::string& usr,
                               const std::string& complete_fp) {
    return storage::delete_file_or_dir(fd, StorageServiceType::FILE_DELE, usr,
                                       complete_fp);
}
StorageServiceResp delete_dir(int fd, const std::string& usr,
                              const std::string& complete_fp) {
    return storage::delete_file_or_dir(fd, StorageServiceType::DIR_DELE, usr,
                                       complete_fp);
}

// CREATE OR QUERY DIR
StorageServiceResp create_dir(int fd, const std::string& usr,
                              const std::string& complete_fp) {
    return storage::create_or_query_dir(fd, StorageServiceType::DIR_CREATE, usr,
                                        complete_fp);
}

StorageServiceResp query_dir(int fd, const std::string& usr,
                             const std::string& complete_fp) {
    return storage::create_or_query_dir(fd, StorageServiceType::DIR_QUERY, usr,
                                        complete_fp);
}

StorageServiceResp query_movable_dir(int fd, const std::string& usr, const std::string& fp) {
    return storage::query_movable_dir(fd, usr, fp);
}

#endif