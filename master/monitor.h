#ifndef MONITOR_H_
#define MONITOR_H_

#include "master_config.h"
#include "../common/hash.h"
#include "../common/file_operation.h"
#include "../common/tcp_operation.h"
#include "../common/kv_interface.h"

static std::unordered_map<int, Address> frontends;
static uint32_t group_size = 0;

enum Backend_State{
    ALIVE = 0,
    DEAD = -1, // Kill by admin
    CRASH = -2, // Ctrl-C
};

struct Backend{
    Backend_State state;
    int fd;
    uint32_t group_id;
    Address addr;

    Backend(){
        state = CRASH;
        fd = -1;
        group_id = -1;
    }
};

static std::vector<Backend> backends;

static std::vector<Backend> alive_backends;
// Dead/Crash backend servers
static std::vector<Backend> dead_backends;

// Hash user to one cluster
uint32_t get_group_id(std::string usr){
    return hashh(usr, 3) % group_size;
}

uint32_t get_group_id(Address addr){
    for(auto it = backends.begin();it != backends.end();++it){
        if(it->addr.name == addr.name){
            return it->group_id;
        }
    }
    return -1;
}

// Get all (alive) server addresses in the same cluster
std::vector<Address> get_cluster(Address addr){
    std::vector<Address> ret;
    uint32_t group_id = get_group_id(addr);

    for(auto it = alive_backends.begin();it != alive_backends.end();++it){
        if(it->group_id == group_id){
            ret.push_back(it->addr);
            debug("Alive %s in %d\n", it->addr.name.c_str(), group_id);
        }
    }
    return ret;
}

int get_fd(Address addr){
    for(auto it = backends.begin();it != backends.end();++it){
        if(it->addr.name == addr.name){
            return it->fd;
        }
    }
    return -1;
}

// Broadcast alive (alive) server addresses to all servers in the same cluster
bool broadcast_group_addr(std::vector<Address> vec){
    std::vector<std::string> names;
    for(auto addr : vec)
        names.push_back(addr.name);

    for(auto addr : vec){
        int fd = tcp_client_socket(addr);
        if(fd > 0){
            if(kv_cluster(fd, names) != FINISHED){
                warn("Fail to send kv_cluster\n");
            }
        }
        else{
            warn("Fail to connect\n");
        }
        debug("Broadcast to %s\n", addr.name.c_str());
    }
    return true;
}

// Get the primary backend server of a user
bool get_primary(std::string usr, Backend& backend){
    uint32_t group_id = get_group_id(usr);

    for(auto it = alive_backends.begin();it != alive_backends.end();++it){
        if(it->group_id == group_id){
            backend = *it;
            return true;
        }
    }
    return false;
}

// Kill one backend server
bool kill_backend(Address addr){
    for(auto it = backends.begin();it != backends.end();++it){
        if(it->addr.name == addr.name && it->state == ALIVE){
            it->state = DEAD;
            break;
        }
    }

    for(auto it = alive_backends.begin();it != alive_backends.end();++it){
        if(it->addr.name == addr.name){
            it->state = DEAD;
            dead_backends.push_back(*it);
            alive_backends.erase(it);
            broadcast_group_addr(get_cluster(addr));

            int fd = tcp_client_socket(addr);
            if(fd > 0){
                if(kv_kill(fd) != FINISHED){
                    warn("Fail to send kv_kill\n");
                }
            }
            else{
                warn("fd < 0 in kv_kill\n");
            }

            return true;
        }
    }
    return false;
}

// Restart one backend server
bool restart_backend(Address addr){
    for(auto it = backends.begin();it != backends.end();++it){
        if(it->addr.name == addr.name && it->state == DEAD){
            it->state = ALIVE;
            break;
        }
    }

    for(auto it = dead_backends.begin();it != dead_backends.end();++it){
        if(it->addr.name == addr.name && it->state == DEAD){
            it->state = ALIVE;
            alive_backends.push_back(*it);
            dead_backends.erase(it);
            broadcast_group_addr(get_cluster(addr));

            int fd = tcp_client_socket(addr);
            if(fd > 0){
                if(kv_restart(fd) != FINISHED){
                    warn("Fail to send kv_restart\n");
                }
            }
            else{
                warn("fd < 0 in kv_restart\n");
            }

            return true;
        }
    }
    return false;
}

// One connection to a backend server is broken (crash)
bool delete_backend_fd(int fd){
    for(auto it = backends.begin();it != backends.end();++it){
        if(it->fd == fd){
            it->fd = -1;
            it->state = CRASH;
            break;
        }
    }

    Address addr;
    for(auto it = alive_backends.begin();it != alive_backends.end();++it){
        if(it->fd == fd){
            it->fd = -1;
            it->state = CRASH;
            addr = it->addr;
            dead_backends.push_back(*it);
            alive_backends.erase(it);
            broadcast_group_addr(get_cluster(addr));
            return true;
        }
    }

    for(auto it = dead_backends.begin();it != dead_backends.end();++it){
        if(it->fd == fd){
            it->fd = -1;
            it->state = CRASH;
            return true;
        }
    }
    
    return false;
}

// One connection to a backend server is set up (alive)
bool set_backend_fd(Address addr, int fd){
    for(auto it = backends.begin();it != backends.end();++it){
        if(it->addr.name == addr.name){
            it->fd = fd;
            it->state = ALIVE;
            break;
        }
    }

    for(auto it = dead_backends.begin();it != dead_backends.end();++it){
        if(it->addr.name == addr.name){
            it->fd = fd;
            it->state = ALIVE;
            alive_backends.push_back(*it);
            dead_backends.erase(it);
            broadcast_group_addr(get_cluster(addr));
            return true;
        }
    }
    return false;
}

// Initialize backend servers based on the config file (backend.txt)
bool initialize_backend(){
    std::string text;
    if(!read_file(BACKEND_PATH, text)){
        return false;
    }

    std::stringstream ss(text);
    std::string line;

	std::unordered_map<std::string, std::set<Address>> addr_group;
    Address address;

	while(std::getline(ss, line, '\n')) {
		size_t pos = line.find(",");
		if(pos != std::string::npos){
			std::string index = line.substr(0, pos);
			std::string str = line.substr(pos + 1);
			address.init(str);
			debug("%s: Backend address %s\n", index.c_str(), str.c_str());
			addr_group[index].insert(address);
		}
    }

    uint32_t index = 0;
    for(auto it = addr_group.begin();it != addr_group.end();++it){
        auto st = it->second;
        for(auto addr = st.begin();addr != st.end();++addr){
            Backend backend;
            backend.group_id = index;
            backend.addr = *addr;
            backends.push_back(backend);
            dead_backends.push_back(backend);
        }
        index += 1;
    }

    group_size = index;
    return true;
}

#endif
