#ifndef STORAGE_REPLY_H_
#define STORAGE_REPLY_H_

#include <regex>

#include "../common/file_operation.h"
#include "storage_interface.h"
#include <ctype.h>

const std::regex kRenameRegex = std::regex("rename(file|folder)(\\/?[a-zA-Z0-9_\\.\\/\\-\\+\\_=]*)=([a-zA-Z0-9_\\.\\/\\-\\+\\_=]*)");
const std::regex kNewfolderRegex = std::regex("newfolder=([a-zA-Z0-9_\\.\\/\\-\\+\\_=]*)");
const std::regex kMoveRegex = std::regex("move(file|folder)(\\/?[a-zA-Z0-9_\\.\\/\\-\\+\\_=]*)=([a-zA-Z0-9_\\.\\/\\-\\+\\_=]*)");
const std::regex kDeleteRegex = std::regex("delete(file|folder)=([a-zA-Z0-9_\\.\\/\\-\\+\\_=]*)");
const std::regex kDownloadRegex = std::regex("download=([a-zA-Z0-9_\\.\\/\\-\\+\\_=]*)");

/* extract uploaded file content from request body */
std::string get_file_content(std::string req_body) {
    req_body = req_body.substr(req_body.find("\r\n\r\n") + 4);

    int end_pos = -1;
    for (int i = req_body.size() - 6; i >= 0; i--) {
        if (req_body[i] == '\r') {
            end_pos = i;
            break;
        }
    }

    return req_body.substr(0, end_pos);
}

bool valid_char(const char c){
	if(c <= 0x20 || c > 0x7e)
		return false;
	if(!isalpha(c) && !isdigit(c) && c != '.' && c != '-'  && c != '_'){
        debug("#Storage_reply invalid char is %c\n", c);
		return false;
    }
	return true;
}

/**
 * filename only support alpha, number, ".", "_", "-"
*/
bool valid_filename(std::string usr){
	for(char c : usr){
		if(!valid_char(c))
			return false;
	}
	return true;
}

/**
 * extract filename from HTTP body
*/
std::string get_file_name(const std::string& body) {
    std::vector<std::string> vec;
    vec = split(body, '\r');
    vec = split(vec.at(1), ';');

    size_t first = vec.at(2).find("\"");
    size_t last = vec.at(2).find_last_of("\"");

    std::string filename = vec.at(2).substr(first + 1, last - first - 1);
    return filename;
}

/**
 * Retrieve directory information for a folder from KV store, and display in html
*/
void display_files(std::string usr, const std::string& req_path, Response& response) {
    std::string html;
    read_file(ABSOLUTE_DIR + "/html/file.html", html);
    std::string current_dir = "/";  // path to dir
    std::string upper_dir = "";
    if (req_path.length() > 5) {
        current_dir = req_path.substr(5);  // /file/d1/d2 --> /d1/d2 remove /file
    }

    // when root folder /d2--> ""
    // else /d1/d2 --> /d1
    size_t last = req_path.find_last_of('/');
    if (last > 0) {
        upper_dir = req_path.substr(0, last);
    }
    // for html href usage
    html = replace(html, "$upperdir", upper_dir);
    html = replace(html, "$current_folder", current_dir);

    std::string replaced_files = "";
    int sockfd = -1;

    std::string backend;
	if(usr_to_address(master_fd, usr, backend)){
		Address dst;
    	dst.init(backend);
		sockfd = tcp_client_socket(dst);
	}
    //TODO: handle sockfd < 0

    int idx = 1;
    // query directory info from KVSTORE
    auto resp = query_dir(sockfd, usr, current_dir);
    if (resp.status() == StorageServiceResp::SUCCESS) {
        for (const auto& entry : resp.dir_info().entries()) {
            std::string one_file_replaced; // used for one_file html display
            std::string DOWNLOADBUTTON = "<button type=\"submit\" class=\"dropdown-item\" value=\"$foldername\" name=\"download\" href = \"/file$current_folder\">Download</button>";

            read_file(ABSOLUTE_DIR + "/html/one_file.html", one_file_replaced);

            // entry.name : /eileen/d1/d2, need to get /d1/d2
            size_t split_ = entry.name().find(usr.c_str());
            std::string folder_path = entry.name().substr(
                split_ + usr.length());  // remove username
            one_file_replaced = replace(one_file_replaced, "$foldername", folder_path);
            DOWNLOADBUTTON = replace(DOWNLOADBUTTON, "$foldername", folder_path);
            one_file_replaced = replace(one_file_replaced, "$index",
                               std::to_string(idx));
            idx++;

            // /eileen/d1/d2, need to get d2
            size_t last = entry.name().find_last_of("/");
            std::string filename = entry.name().substr(last + 1);
            one_file_replaced = replace(one_file_replaced, "$filename", filename);
            one_file_replaced = replace(one_file_replaced, "$current_folder",
                               current_dir);
            DOWNLOADBUTTON = replace(DOWNLOADBUTTON, "$current_folder",
                               current_dir);


            // handle file or folder
            if (entry.is_dir()) {
                one_file_replaced = replace(one_file_replaced, "$filetype", "folder");
                one_file_replaced = replace(one_file_replaced, DOWNLOADBUTTON, "");
            } else {
                one_file_replaced = replace(one_file_replaced, "$filetype", "file");
            }

            

            auto movable_resp =
                query_movable_dir(sockfd, usr, folder_path);
            std::string options;
            if (resp.status() == StorageServiceResp::SUCCESS) {
                for (const auto& entry :
                     movable_resp.movable_dirs().entries()) {
                    // entry.name : /eileen/d1/d2, need to get /d1/d2
                    size_t split_ = entry.name().find(usr.c_str());
                    std::string folder_path = entry.name().substr(
                        split_ + usr.length());  // remove username
                    if (folder_path.empty()) {
                        continue;
                    }
                    std::string one_option = "<option value=\"" +
                                             folder_path + "\">" +
                                             folder_path + "</option>";
                    options += one_option;
                }
            }

            one_file_replaced = replace(one_file_replaced, "$optionholder", options);

            replaced_files += one_file_replaced;
        }
    }

    html = replace(html, "$one_file_class", replaced_files);
    response.body_ = html;
    response.headers_[CONTENT_LEN] = std::to_string(response.body_.length());
    close(sockfd);
}

Response storage_handler(Request& req) {
    Response response;

    std::string usr = req.username_;
    int sockfd = -1;

    std::string backend;
	if(usr_to_address(master_fd, usr, backend)){
		Address dst;
    	dst.init(backend);
		sockfd = tcp_client_socket(dst);
	}
    //TODO: handle sockfd < 0

    response.status_ = OK;
    response.headers_[CONTENT_TYPE] = "text/html";

    // if not logged in, GET method means displaying login page
    if (req.method_ == "GET") {
        display_files(usr, req.path_, response);
    }
    else if (req.method_ == "POST") {
        debug_v3("Print POST req [%s], %s, %s\n", req.method_.c_str(),
            req.path_.c_str(), req.body_.c_str());
        std::string req_body_without_parse = req.body_;
        req.body_ = http_parse(req.body_);

        std::smatch match;

        StorageServiceResp resp;
        resp.set_status(StorageServiceResp::FAIL);

        // create new folder
        if (std::regex_search(req.body_, match, kNewfolderRegex)) {
            std::string foldername = match[1];
            bool valid = valid_filename(foldername);
            if (!valid){
                response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/file_err.html");
              response.body_ = replace(response.body_, "$error", "INVALID NAME");
                warn("Fail to create folder\n");  
            } 
            if (req.path_.at(req.path_.length() - 1) != '/') {
                foldername = req.path_.substr(5) + "/" + foldername;
            } else {
                foldername = req.path_.substr(5) + foldername;
            }

            debug_v2("creating folder %s\n", foldername.c_str());
            if (valid)  {
                resp = create_dir(sockfd, usr, foldername);
                if (resp.status() == StorageServiceResp::SUCCESS) {
                    debug("User create folder %s\n", foldername.c_str());
                    display_files(usr, req.path_, response);

                } else {
                    response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/file_err.html");
                    response.body_ = replace(response.body_, "$error", "CANNOT CREATE FOLDER");
                    warn("Fail to create folder\n");
                }
            }
        } 
        // upload file
        else if (req.body_.find("name=\"uploadfile\"") !=
                    std::string::npos) {
            std::string uploaded_name, filename;
            if (req.path_.length() > 6) {
                uploaded_name += "/" + req.path_.substr(6) + "/";
            }
            filename = get_file_name(req.body_);
            uploaded_name += filename;
            std::string content = get_file_content(req_body_without_parse);
            // Start of storage Service handle upload file requests.
            if (!valid_filename(filename)){
                response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/file_err.html");
                response.body_ = replace(response.body_, "$error", "FILE NAME INVALID");
            }
            else {
                resp = upload_file(/*fd=*/sockfd, usr, uploaded_name,
                                    content);
                if (resp.status() == StorageServiceResp::SUCCESS) {
                    debug("User upload file %s\n", uploaded_name.c_str());
                    std::cout << "File Size: " << content.size() << std::endl;
                    display_files(usr, req.path_, response);

                } else {
                    response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/file_err.html");
                    response.body_ = replace(response.body_, "$error", "CANNOT UPLOAD FILE");
                    warn("Failed to upload file with resp %s",
                        uploaded_name.c_str());
                }
            }
        }
        // rename folder or directory
        else if (std::regex_search(req.body_, match, kRenameRegex)) {
            std::string type = match[1];
            std::string original_name_with_path = match[2];
            std::string new_name = match[3];

            if (type.compare("folder") == 0) {
                debug("renaming folder %s to %s\n",
                      original_name_with_path.c_str(), new_name.c_str());
                resp = rename_dir(/*fd=*/sockfd, usr,
                                  original_name_with_path, new_name);
            } else if (type.compare("file") == 0) {
                debug("renaming file %s to %s\n",
                        original_name_with_path.c_str(), new_name.c_str());
                resp = rename_file(/*fd=*/sockfd, usr,
                                   original_name_with_path, new_name);
            }
            
            if (resp.status() == StorageServiceResp::SUCCESS) {
                    debug("User rename %s to %s\n",
                        original_name_with_path.c_str(), new_name.c_str());
                    display_files(usr, req.path_, response);

            }
            else {
                    response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/file_err.html");
                    response.body_ = replace(response.body_, "$error", "RENAME FAILED");
                    warn("Failed to rename file with resp %s",
                        resp.DebugString().c_str());
            }
            
        }
        
        // move file or directory
        else if (std::regex_search(req.body_, match, kMoveRegex)) {
            std::string type = match[1];
            std::string original_name_with_path = match[2];
            std::string new_name = match[3];
            

            if (type.compare("file") == 0) {
                debug("move file %s to %s\n",
                      original_name_with_path.c_str(), new_name.c_str());
                resp = move_file(/*fd=*/sockfd, usr,
                                 original_name_with_path, new_name);
            }

            else if (type.compare("folder") == 0) {
                debug("move folder %s to %s\n",
                      original_name_with_path.c_str(), new_name.c_str());
                resp = move_dir(/*fd=*/sockfd, usr,
                                original_name_with_path, new_name);
            }

            if (resp.status() == StorageServiceResp::SUCCESS) {
                debug("User move %s to %s\n",
                      original_name_with_path.c_str(), new_name.c_str());
                display_files(usr, req.path_, response);

            } else {
                response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/file_err.html");
                response.body_ = replace(response.body_, "$error", "MOVE FAILED");
                warn("Failed to move file with resp %s",
                     resp.DebugString().c_str());
            }
        }

        // delete file or directory
        else if (std::regex_search(req.body_, match, kDeleteRegex)) {
            std::string type = match[1];
            std::string full_path = match[2];
            

            if (type.compare("folder") == 0) {
                resp = delete_dir(/*fd=*/sockfd, usr, full_path);
            } 
            else if (type.compare("file") == 0){
                resp = delete_file(/*fd=*/sockfd, usr, full_path);
            }
            if (resp.status() == StorageServiceResp::SUCCESS) {
                debug("User delete file %s\n", full_path.c_str());
                display_files(usr, req.path_, response);
            } else {
                response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/file_err.html");
                response.body_ = replace(response.body_, "$error", "DELETE FAILED");
                warn("Failed to delete file with resp %s",
                     resp.DebugString().c_str());
            }
        }

        // download file
        else if (std::regex_search(req.body_, match, kDownloadRegex)) {
            std::string download_path_name = match[1];
            
            size_t last_slash = req.body_.find_last_of("/");
            std::string download_file_name =
                req.body_.substr(last_slash + 1);

            if (download_file_name.find("jpeg") != std::string::npos ||
                download_file_name.find("jpg") != std::string::npos) {
                response.headers_[CONTENT_TYPE] = "image/jpeg";
            } else if (download_file_name.find("png") !=
                       std::string::npos) {
                response.headers_[CONTENT_TYPE] = "image/png";
            } else if (download_file_name.find("pdf") !=
                       std::string::npos) {
                response.headers_[CONTENT_TYPE] = "application/pdf";
            } else {
                response.headers_[CONTENT_TYPE] = "text/plain";
            }
            response.headers_[CONTENT_DISPOSITION] =
                "attachment; filename=" + download_file_name + ";";
            resp =
                download_file(/*fd=*/sockfd, usr, download_path_name);
            if (resp.status() == StorageServiceResp::SUCCESS) {
                debug("User downloaded file content with size %ld\n",
                      resp.file_download().size());
                response.body_ = resp.file_download();
                response.headers_[CONTENT_LEN] = std::to_string(response.body_.length());
            }
            else {
                
                response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/file_err.html");
                response.body_ = replace(response.body_, "$error", "DOWNLOAD FAILED");
                warn("Failed to upload file with resp %s",
                     resp.DebugString().c_str());
            }
        }
    }
    close(sockfd);
    return response;
}

#endif