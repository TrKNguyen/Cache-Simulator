#include<iostream> 
#include<fstream>
#include<string> 
#include<unordered_map> 
#include<list> 
#include<vector> 
#include<utility> 
#include<queue>
#include<iomanip>
#include<optional>
#include<functional> 
#include <cassert>

std::string protocol; 
std::string input_file; 
int cache_size; 
int associativity; 
int block_size = 32; // 32 bytes by default
const int word_size = 4; // 4 bytes; 
const int base = 19102004; 
const int ram_access = 100; 
const int cache_access = 1;

class Bus; 
class LRU_Cache; 
class Core; 
class Operating_System; 
enum MESI{M, E, S, I}; 

struct Monitor {
    // Cache calculate 
    int overall_cyc = 0; 
    std::vector<int> compute_cyc; 
    std::vector<int> ls_ins; 
    std::vector<int> idle_cyc; 
    std::vector<std::pair<int, int>> hit_miss_cnt; 
    int bus_data_traffic = 0; 
    int bus_invalidate_update_cnt = 0; 
    
    // FIX: Separate private vs shared
    int private_data_accesses = 0;  // Access to M or E state
    int shared_data_accesses = 0;   // Access to S state

    int num_cores;
    Monitor(int num_cores): num_cores(num_cores) {
        compute_cyc = std::vector<int>(num_cores, 0);
        ls_ins = std::vector<int>(num_cores, 0);
        idle_cyc = std::vector<int>(num_cores, 0); 
        hit_miss_cnt = std::vector<std::pair<int, int>>(num_cores, {0, 0}); 
    }
    void print_statistics() const {
        std::cout << "\n========================================" << std::endl;
        std::cout << "        SIMULATION RESULTS" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        // 1. Overall execution cycles
        std::cout << "1. Overall Execution Cycles: " << overall_cyc << std::endl;
        std::cout << std::endl;
        
        // 2. Compute cycles per core
        std::cout << "2. Compute Cycles:" << std::endl;
        for (int i = 0; i < num_cores; i++) {
            std::cout << "   Core " << i << ": " << compute_cyc[i] << std::endl;
        }
        std::cout << std::endl;
        
        // 3. Load/store instructions per core
        std::cout << "3. Load/Store Instructions:" << std::endl;
        for (int i = 0; i < num_cores; i++) {
            std::cout << "   Core " << i << ": " << ls_ins[i] << std::endl;
        }
        std::cout << std::endl;
        
        // 4. Idle cycles per core
        std::cout << "4. Idle Cycles:" << std::endl;
        for (int i = 0; i < num_cores; i++) {
            std::cout << "   Core " << i << ": " << idle_cyc[i] << std::endl;
        }
        std::cout << std::endl;
        
        // 5. Cache hit/miss counts per core
        std::cout << "5. Cache Statistics:" << std::endl;
        for (int i = 0; i < num_cores; i++) {
            int hits = hit_miss_cnt[i].first;
            int misses = hit_miss_cnt[i].second;
            int total = hits + misses;
            double hit_rate = total > 0 ? (100.0 * hits / total) : 0.0;
            
            std::cout << "   Core " << i << ":" << std::endl;
            std::cout << "      Hits:   " << hits << std::endl;
            std::cout << "      Misses: " << misses << std::endl;
            std::cout << "      Hit Rate: " << std::fixed << std::setprecision(2) 
                      << hit_rate << "%" << std::endl;
        }
        std::cout << std::endl;
        
        // 6. Bus data traffic
        std::cout << "6. Bus Data Traffic: " << bus_data_traffic << " bytes" << std::endl;
        std::cout << std::endl;
        
        // 7. Invalidations or updates
        std::cout << "7. Invalidations/Updates on Bus: " 
                  << bus_invalidate_update_cnt << std::endl;
        std::cout << std::endl;
        
        // 8. Private vs shared data distribution
        std::cout << "8. Data Access Distribution:" << std::endl;
        std::cout << "   Private: " << private_data_accesses << std::endl;
        std::cout << "   Shared:  " << shared_data_accesses << std::endl;
        
        std::cout << "\n========================================\n" << std::endl;
    }
}; 
enum Cmd {BusRd, BusRdX, FlushWB}; 

struct BusTxn {
    Cmd cmd; 
    int address;
    int src_core; 
    int remaining_cycles;
    bool supplied_by_cache = false;
    int supplier_core = -1;
    int bytes_on_bus = 0;
    bool caused_invalidation = false;  // Track if this transaction invalidated any cache
    
    BusTxn(Cmd cmd, int address, int src_core, int remaining_cycles, bool supplied_by_cache, int supplier_core, int bytes_on_bus) {
        this->cmd = cmd; 
        this->address = address; 
        this->src_core = src_core; 
        this->remaining_cycles = remaining_cycles; 
        this->supplied_by_cache = supplied_by_cache; 
        this->supplier_core = supplier_core; 
        this->bytes_on_bus = bytes_on_bus; 
        this->caused_invalidation = false;
    } 
}; 

class Bus {
private: 
    Operating_System* operating_system;
    std::queue<BusTxn> pending;
    std::optional<BusTxn> active;
    int waiting_io; 
    int* global_cycle;
    Monitor* monitor;
public: 
    Bus(Operating_System* operating_system, int* global_cycle, Monitor* monitor): operating_system(operating_system), global_cycle(global_cycle), monitor(monitor) {
        waiting_io = -1;
    }
    void tick();
    void request(Cmd cmd, int address, int src_core, int block_size);
}; 

class LRU_Cache {
private: 

    // LRU_Cache data member

    class Entry {
    public: 
        int address;
        std::vector<int> words;
        MESI state;

        Entry() {}
        Entry(int address, std::vector<int> words, MESI state): address(address), words(words), state(state) {}
    };
    class Request {
    public: 
        Cmd cmd; 
        int address; 
        int id; 
        int block_size;
        Request(Cmd cmd, int address, int id, int block_size): cmd(cmd), address(address), id(id), block_size(block_size) {} 
    }; 
    // map from tag + index (tag + index = block memory address) -> corresponding location(iterator)
    std::unordered_map<int, std::list<Entry>::iterator> entry_location;
    std::vector<std::list<Entry>> cache_sets; 
    int associativity;
    int cache_size;
    int number_sets;
    int block_size;
    // Bus;
    Bus* bus;
    // monitor 
    Monitor* monitor;
    
    int *global_cycle; 

    Core* core;
    int n_waiting_io;
    std::queue<Request> requests;
public: 
    LRU_Cache(Core* core, int cache_size, int associativity, int block_size, int* global_cycle, Monitor* monitor, Bus* bus): core(core), cache_size(cache_size), associativity(associativity), block_size(block_size), global_cycle(global_cycle), monitor(monitor), bus(bus)  {
        number_sets = cache_size / (associativity * block_size);
        cache_sets = std::vector<std::list<Entry>>(number_sets, std::list<Entry>()); 
        n_waiting_io = 0;
    }
    void reorder(int index, int block_memory) {
        auto it = entry_location[block_memory];
        cache_sets[index].splice(cache_sets[index].begin(), cache_sets[index], it);
        entry_location[block_memory] = cache_sets[index].begin();

    }
    bool evict(int index) {
        auto& evict_entry = *(--cache_sets[index].end()); 
        auto state = evict_entry.state;
        int address = evict_entry.address; 
        int block_memory = address / block_size; 
        int tag = block_memory / number_sets; 
        
        entry_location.erase(block_memory);
        cache_sets[index].erase(--cache_sets[index].end());
        if (state == MESI::M) {
            store_words_to_ram(address);
        }
        return state == MESI::M;
    }
    void print() {
        std::cout <<"print cache set\n"; 
        for (auto ls: cache_sets) {
            for (auto e: ls) {
                std::cout << e.address <<" ";
            }
        }
        std::cout <<"end cache set\n"; 
    }    
    void store_words_to_ram(int address);
    std::vector<int> load_words_from_ram(int address);
    bool snoop(Cmd cmd, int address, std::optional<BusTxn>& active);  // Returns true if invalidation occurred
    std::pair<int, bool> get(int address);
    void put(int address, int word);
    void notify_finish_io();
    
    void update_entry_state(int address, MESI state);
};



class Core {
private: 
    LRU_Cache* cache; 
    std::ifstream fin; 

    Monitor* monitor; 
    int* global_cycle;
    Bus* bus;
    bool waiting_io; 
    int waiting_cal;
    int cnt = 0;
    int id;
public: 
    Core(int id, std::string input_file, int* global_cycle, Monitor* monitor, Bus* bus): id(id), global_cycle(global_cycle), monitor(monitor), bus(bus)  {
        cache = new LRU_Cache(this, cache_size, associativity, block_size, global_cycle, monitor, bus); 
        std::string filename = "./" + input_file + "_four/" + input_file + "_" + std::to_string(id) + ".data";
        std::cout << "Loading file: " << filename << std::endl;
        fin = std::ifstream(filename);
        if (!fin.is_open()) {
            std::cerr << "ERROR: Could not open file: " << filename << std::endl;
            exit(1);
        }
        waiting_io = false; 
        waiting_cal = -1;
        
    }
    int hex_to_dec(std::string address_string) {
        int address = 0; 
        for (int i = 2; i < address_string.size(); i++) {
            if (address_string[i] >= 'a' && address_string[i] <= 'f') {
                address = address * 16 + (address_string[i] - 'a' + 10);
            } else {
                address = address * 16 + (address_string[i] - '0');
            }
        }
        return address;
    }
    bool execute_next_instruction() {
        if (waiting_cal > *global_cycle) {
            return true;
        }
        if (waiting_io) {
            monitor->idle_cyc[id]++;
            return true;
        }
        int type; 
        std::string address_string; 
        if (fin.eof()) {
            return false;
        }
        fin >> type >> address_string;
        
        if (fin.fail()) {
            return false;
        }
        
        int address = hex_to_dec(address_string);
        if (type == 0) { 
            waiting_io = true;
            auto result = cache->get(address); 
            monitor->ls_ins[id]++;
        } else if (type == 1) {
            waiting_io = true;
            cache->put(address, base);
            monitor->ls_ins[id]++;
        } else {
            int cal_cycles = address;
            waiting_cal = *global_cycle + cal_cycles;
            monitor->compute_cyc[id] += cal_cycles;
        }
        return true;
    }
    void finish_io() {
        waiting_io = false;
    }
    int get_id() {
        return id;
    }
    LRU_Cache* get_cache() {
        return cache;
    }
}; 

class Operating_System {
private: 
    int* global_cycle;
    std::vector<Core*> cores; 
    Bus* bus; 
    int n_cores;
    Monitor* monitor;
public: 
    Operating_System(int n_cores): n_cores(n_cores) {
        global_cycle = new int(0);
        monitor = new Monitor(n_cores);
        bus = new Bus(this, global_cycle, monitor);
        cores = std::vector<Core*>(n_cores); 
        for (int i = 0; i < n_cores; i++) {
            cores[i] = new Core(i, input_file, global_cycle, monitor, bus);
        }
    }
    void run() {
        while (1) {
            bus->tick();
            bool check_all_finish = true; 
            for (int i = 0; i < n_cores; i++) {
                bool still_running = cores[i]->execute_next_instruction();
                check_all_finish = (check_all_finish && !still_running);
            }
            if (check_all_finish) {
                monitor->overall_cyc = *global_cycle - 1;
                break;
            }
            (*global_cycle)++;
        }
        monitor->print_statistics();
    }
    std::vector<Core*>& get_cores() {
        return cores;
    }
}; 

// Implementation of LRU_Cache methods

void LRU_Cache::store_words_to_ram(int address) {
    n_waiting_io++;
    requests.push(Request(Cmd::FlushWB, address, this->core->get_id(), block_size));
    if (requests.size() == 1) {
        auto& request = requests.front();
        bus->request(request.cmd, request.address, request.id, request.block_size);
        requests.pop();
    }
}

std::vector<int> LRU_Cache::load_words_from_ram(int address) {
    int index = (int)(address / block_size) % number_sets; 
    if (cache_sets[index].size() >= (size_t)associativity) {
        bool check_write_back = evict(index);
    }
    n_waiting_io++;
    requests.push(Request(Cmd::BusRd, address, this->core->get_id(), block_size));
    if (requests.size() == 1) {
        auto& request = requests.front();
        bus->request(request.cmd, request.address, request.id, request.block_size);
        requests.pop();
    }
    
    return std::vector<int>(block_size / word_size, 0);
}

// FIX: Return true if invalidation occurred
bool LRU_Cache::snoop(Cmd cmd, int address, std::optional<BusTxn>& active) {
    int block_memory = address / block_size; 
    int tag = block_memory / number_sets; 
    if (!entry_location.count(block_memory)) {
        return false; 
    }
    Entry& entry= *entry_location[block_memory];
    
    bool invalidated = false;
    
    if (cmd == Cmd::BusRd) {
        if (entry.state == MESI::M) {
            (*active).supplier_core = core->get_id();
            (*active).supplied_by_cache = true;
            entry.state = MESI::S;
        } else if (entry.state == MESI::E) {
            (*active).supplier_core = core->get_id();
            (*active).supplied_by_cache = true;
            entry.state = MESI::S;
        } 
    } else if (cmd == Cmd::BusRdX) {
        if (entry.state != MESI::I) {  // If it was valid before
            if (entry.state == MESI::M) {
                (*active).supplier_core = core->get_id();
                (*active).supplied_by_cache = true;
            }
            entry.state = MESI::I;
            invalidated = true;  // Mark that we invalidated
        }
    } else if (cmd == Cmd::FlushWB) {
        // do nothing 
    }
    
    return invalidated;
}

std::pair<int, bool> LRU_Cache::get(int address) {
    int block_memory = address / block_size; 
    int tag = block_memory / number_sets; 
    int index = block_memory % number_sets; 
    int offset = (address % block_size) / word_size;
    
    if (entry_location.count(block_memory)) {
        Entry& entry = *entry_location[block_memory];
        
        // FIX: Track private vs shared BEFORE state transition
        if (entry.state == MESI::M || entry.state == MESI::E) {
            monitor->private_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].first++;
        } else if (entry.state == MESI::S) {
            monitor->shared_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].first++;
        } else if (entry.state == MESI::I) { 
            monitor->hit_miss_cnt[core->get_id()].second++;
            n_waiting_io++;
            requests.push(Request(Cmd::BusRd, address, this->core->get_id(), block_size));
            if (requests.size() == 1) {
                auto& request = requests.front();
                bus->request(request.cmd, request.address, request.id, request.block_size);
                requests.pop();
            }
            entry.state = MESI::S;
            monitor->shared_data_accesses++;  // After reload, it's shared
        }
        reorder(index, block_memory);
    } else {
        // Cache miss
        monitor->hit_miss_cnt[core->get_id()].second++;
        auto words = load_words_from_ram(address);
        cache_sets[index].insert(cache_sets[index].begin(), Entry(block_memory * block_size, words, MESI::S));
        entry_location[block_memory] = cache_sets[index].begin();
        monitor->shared_data_accesses++;  // New load is shared initially
    }
    
    auto p = std::make_pair(cache_sets[index].begin()->words[offset], true);
    n_waiting_io++; 
    notify_finish_io();
    return p;
}

void LRU_Cache::put(int address, int word) {
    int block_memory = address / block_size; 
    int tag = block_memory / number_sets; 
    int index = block_memory % number_sets; 
    int offset = (address % block_size) / word_size;
    
    if (entry_location.count(block_memory)) {
        Entry& entry = *entry_location[block_memory];
        
        // FIX: Track private vs shared BEFORE state transition
        if (entry.state == MESI::M || entry.state == MESI::E) {
            monitor->private_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].first++;
        } else if (entry.state == MESI::S) {
            monitor->shared_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].second++;  // Write to S is a miss!
            n_waiting_io++;
            requests.push(Request(Cmd::BusRdX, address, this->core->get_id(), block_size));
            if (requests.size() == 1) {
                auto& request = requests.front();
                bus->request(request.cmd, request.address, request.id, request.block_size);
                requests.pop();
            }
        } else if (entry.state == MESI::I) {
            monitor->hit_miss_cnt[core->get_id()].second++;
            n_waiting_io++;
            requests.push(Request(Cmd::BusRdX, address, this->core->get_id(), block_size));
            if (requests.size() == 1) {
                auto& request = requests.front();
                bus->request(request.cmd, request.address, request.id, request.block_size);
                requests.pop();
            }
        }
        reorder(index, block_memory);
        cache_sets[index].begin()->words[offset] = word;
        cache_sets[index].begin()->state = MESI::M;
        // After write, it becomes private
        if (entry.state != MESI::M && entry.state != MESI::E) {
            monitor->private_data_accesses++;
        }
    } else {
        // Cache miss
        monitor->hit_miss_cnt[core->get_id()].second++;
        auto words = load_words_from_ram(address); 
        words[offset] = word;
        cache_sets[index].insert(cache_sets[index].begin(), Entry(block_memory * block_size, words, MESI::M));
        entry_location[block_memory] = cache_sets[index].begin();
        monitor->private_data_accesses++;  // New write starts as private (M state)
    }
    n_waiting_io++; 
    notify_finish_io();
}

void LRU_Cache::notify_finish_io() {
    n_waiting_io--;
    assert(n_waiting_io >= 0);
    if (requests.size()) {
        auto& request = requests.front();
        bus->request(request.cmd, request.address, request.id, request.block_size);
        requests.pop();
    } 
    if (n_waiting_io <= 0) {
        core->finish_io();
    }
}

void LRU_Cache::update_entry_state(int address, MESI state) {
    int block_memory = address / block_size; 
    if (entry_location.find(block_memory) == entry_location.end()) {
        return;  // Entry doesn't exist, can't update
    }
    Entry& entry = *entry_location[block_memory];
    entry.state = state;
}

// Bus implementations 

void Bus::tick() {
    if (active == std::nullopt || (*active).remaining_cycles == 1) {
        if (active != std::nullopt) {
            (*monitor).bus_data_traffic += (*active).bytes_on_bus;
            
            // FIX: Count invalidations PER TRANSACTION, not per cache
            if ((*active).cmd == Cmd::BusRdX && (*active).caused_invalidation) {
                (*monitor).bus_invalidate_update_cnt++;
            }
            
            auto& cores = this->operating_system->get_cores();
            for (auto& core: cores) {
                if (core->get_id() == (*active).src_core) {
                    core->get_cache()->notify_finish_io();
                    break;
                }
            }
        }
        active = std::nullopt;
        if (pending.size()) {
            active = std::move(pending.front()); 
            pending.pop();
            auto& cores = this->operating_system->get_cores(); 
            
            // Snoop and track if ANY cache was invalidated
            bool any_invalidation = false;
            for (auto& core: cores) {
                if (core->get_id() == (*active).src_core) {
                    continue;
                }
                bool invalidated = core->get_cache()->snoop((*active).cmd, (*active).address, active);
                any_invalidation = any_invalidation || invalidated;
            }
            (*active).caused_invalidation = any_invalidation;
            
            auto cmd = (*active).cmd;
            if (cmd == Cmd::BusRd) {
                if ((*active).supplied_by_cache) {
                    (*active).bytes_on_bus = block_size; 
                    (*active).remaining_cycles = 2 * block_size / word_size;
                } else {
                    (*active).bytes_on_bus = block_size; 
                    (*active).remaining_cycles = 100;
                    for (auto& core: cores) {
                        if (core->get_id() == (*active).src_core) {
                            core->get_cache()->update_entry_state((*active).address, MESI::E);
                            break;
                        }
                    }
                } 
            } else if (cmd == Cmd::BusRdX) {
                if ((*active).supplied_by_cache) {
                    (*active).bytes_on_bus = block_size; 
                    (*active).remaining_cycles = 2 * block_size / word_size;
                } else {
                    (*active).bytes_on_bus = block_size; 
                    (*active).remaining_cycles = 100;
                } 
            } else if (cmd == Cmd::FlushWB) {
                (*active).remaining_cycles = 100;
                (*active).bytes_on_bus = block_size;
            }
        }
    } else {
        (*active).remaining_cycles--;
    }   
}

void Bus::request(Cmd cmd, int address, int src_core, int block_size) {
    if (cmd == Cmd::BusRd) {
        pending.push(BusTxn(cmd, address, src_core, -1, false, -1, -1));
    } else if (cmd == Cmd::BusRdX) {
        pending.push(BusTxn(cmd, address, src_core, -1, false, -1, -1));
    } else if (cmd == Cmd::FlushWB) {
        pending.push(BusTxn(cmd, address, src_core, -1, false, -1, -1));
    }
}


int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " <protocol> <input_file> <cache_size> <associativity> <block_size>" << std::endl;
        return 1;
    }
    
    protocol = argv[1]; 
    input_file = argv[2]; 
    cache_size = std::stoi(std::string(argv[3])); 
    associativity = std::stoi(std::string(argv[4])); 
    block_size = std::stoi(std::string(argv[5]));
    
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Protocol: " << protocol << std::endl;
    std::cout << "  Input: " << input_file << std::endl;
    std::cout << "  Cache size: " << cache_size << " bytes" << std::endl;
    std::cout << "  Associativity: " << associativity << std::endl;
    std::cout << "  Block size: " << block_size << " bytes" << std::endl;
    std::cout << std::endl;
    
    int n_cores = 4; 
    Operating_System operating_system(n_cores); 
    operating_system.run();
    
    return 0;
}