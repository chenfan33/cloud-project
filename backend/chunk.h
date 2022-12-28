#ifndef CHUNK_H_
#define CHUNK_H_

#include <fstream>
#include <utility>
#include <unordered_set>
#include <unordered_map>

#include "kv_config.h"
#include "../common/kv_interface.h"
#include "../common/file_operation.h"

#define SIZE_LIMIT (1 << 26)

typedef std::unordered_map<std::string, std::string> KV_Map;

// Chunk information for each user (in disk)
struct Chunk{
    uint64_t append_index;
    uint64_t current_size;
    
    std::string usr;
    std::string folder;

    // The mapping from key to chunk id
    std::unordered_map<std::string, uint64_t> metadata;
    // The key (and the chunk id) to delete
    std::vector<std::pair<std::string, uint64_t>> del_list;
    
    // Read the chunk information
    int init(std::string _usr){
        usr = _usr;
        folder = PREFIX + usr;
        if(!exist_file(folder.c_str())){
			return USER_ERROR;
		}

        std::string text;

        if(read_file(folder + "/chunk_index", text)){
            auto vec = split(text, '\n');
            append_index = stoull(vec[0]);
            current_size= stoull(vec[1]);
        }
        else{
            append_index = 0;
            current_size = 0;
        }

        if(read_file(folder + "/chunk_metadata", text)){
            auto vec = split(text, '\n');
            for(auto it = vec.begin();it != vec.end();){
                std::string key = *it;
                it += 1;
                if(it == vec.end())
                    break;
                uint64_t value = stoull(*it);
                it += 1;
                metadata[key] = value;
            }
        }

        return FINISHED;
    }

    // Read the value of key in the chunk
    int get_value(std::string key, std::string& value){
        if(metadata.find(key) == metadata.end())
            return KEY_ERROR;
        
        std::string path = folder + "/chunk-" + std::to_string(metadata[key]);
        std::ifstream file(path, std::ios::binary);
        if(!file.is_open())
            return KEY_ERROR;

        std::string _key, _size;
        std::vector<char> _value;
        bool find = false;
        while(std::getline(file, _key)){
            std::getline(file, _size);
            uint64_t size = stoull(_size);
            if(key == _key){
                _value.resize(size);
	            file.read(_value.data(), size);
                value = binary_to_text(_value);
                find = true;
            }
            else{
                file.seekg(size + file.tellg());
            }
        }
        file.close();

        return find? FINISHED : KEY_ERROR;
    }

    // Get all key-value pairs
    KV_Map get_all_kv(){
        KV_Map ret;

        for(auto it = metadata.begin();it != metadata.end();++it){
            std::string key = it->first;
            std::string value;
            get_value(key, value);
            ret[key] = value;
        }

        return ret;
    }

    // Append one key-value pair in chunk
    bool append_kv(std::ofstream& file, std::string key, std::string value){
        std::string data = key + "\n" + std::to_string(value.size())
                    + "\n" + value;
        file.write(data.data(), data.size());
        return true;
    }

    // Append key-value pairs in chunk (for checkpoint)
    int append_kvs(KV_Map mp){
        std::string path = folder + "/chunk-" + std::to_string(append_index);
        std::ofstream file(path, std::ios::binary | std::ios::app);

        for(auto it = mp.begin();it != mp.end();++it){
            if(it->second == ""){
                del_list.push_back({it->first, metadata[it->first]});
                metadata.erase(it->first);
            }
            else{
                if(metadata.find(it->first) != metadata.end()){
                    del_list.push_back({it->first, metadata[it->first]});
                }

                append_kv(file, it->first, it->second);
                metadata[it->first] = append_index;
                
                current_size = file.tellp();
                if(current_size > SIZE_LIMIT){
                    append_index += 1;
                    current_size = 0;
                    file.close();
                    path = folder + "/chunk-" + std::to_string(append_index);
                    file.open(path, std::ios::binary | std::ios::app);
                }
            }
        }

        file.close();
        write_meta_back();
        lazy_delete();
        return FINISHED;
    }

    // Write the metadata back to disk (for checkpoint)
    bool write_meta_back(){
        std::string path = folder + "/chunk_index";
        std::ofstream file(path, std::ios::binary);
        
        std::string data = std::to_string(append_index) + "\n" + std::to_string(current_size);
        file.write(data.data(), data.size());
        file.close();

        path = folder + "/chunk_metadata";
        file.open(path, std::ios::binary);
        for(auto it = metadata.begin();it != metadata.end();++it){
            data = it->first + "\n" + std::to_string(it->second) + "\n";
            file.write(data.data(), data.size());
        }
        file.close();

        path = folder + "/delete_list";
        file.open(path, std::ios::binary | std::ios::app);
        for(auto it = del_list.begin();it != del_list.end();++it){
            data = it->first + "\n" + std::to_string(it->second) + "\n";
            file.write(data.data(), data.size());
        }
        file.close();

        return true;
    }

    // Load one chunk file
    std::vector<std::pair<std::string, std::string>> load_chunk(std::ifstream& file){
        std::vector<std::pair<std::string, std::string>> ret;
        std::string _key, _size;
        std::vector<char> _value;

        while(std::getline(file, _key)){
            std::getline(file, _size);
            uint64_t size = stoull(_size);
            _value.resize(size);
	        file.read(_value.data(), size);
            ret.push_back({_key, binary_to_text(_value)});
        }

        return ret;
    } 

    // Delete key-value pairs in chunks based on the delete list
    bool lazy_delete(){
        std::string text;
        std::string path = folder + "/delete_list";

        if(!read_file(path, text))
            return false;
        
        std::unordered_map<uint64_t, std::unordered_map<std::string, uint32_t>> del_mp;

        auto vec = split(text, '\n');
        for(auto it = vec.begin();it != vec.end();){
            std::string key = *it;
            it += 1;
            if(it == vec.end())
                break;
            uint64_t value = stoull(*it);
            it += 1;
            del_mp[value][key] += 1;
        }

        for(auto it = del_mp.begin();it != del_mp.end();++it){
            uint64_t index = it->first;
            std::string path = folder + "/chunk-" + std::to_string(index);
            std::ifstream file(path, std::ios::binary);
            auto vec = load_chunk(file);
            file.close();

            auto mp = it->second;
            for(auto kv = vec.begin();kv != vec.end();){
                if(mp[kv->first] > 0){
                    mp[kv->first] -= 1;
                    kv = vec.erase(kv);
                }
                else{
                    kv += 1;
                }
            }

            std::ofstream out(path, std::ios::binary);
            for(auto kv = vec.begin();kv != vec.end();++kv){
                append_kv(out, kv->first, kv->second);
            }
            out.close();
        }

        remove_file(path.c_str());
        return true;
    }
};

#endif