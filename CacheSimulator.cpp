#include<iostream> 
#include<fstream>
#include<string> 
#include<unordered_map> 
#include<list> 
#include<vector> 
#include<utility> 
#include<queue>
#include <iomanip>


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

struct Monitor {
    // Cache calculate 
    int overall_cyc = 0; 
    std::vector<int> compute_cyc; 
    std::vector<int> ls_ins; 
    std::vector<int> idle_cyc; 
    std::vector<std::pair<int, int>> hit_miss_cnt; 
    int bus_data_traffic = 0; 
    int bus_invalidate_update_cnt = 0; 
    int distribution = 0; 

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
        std::cout << "   Private: " << distribution << std::endl;
        std::cout << "   Shared:  " << 0 << " (N/A for single core)" << std::endl;
        
        std::cout << "\n========================================\n" << std::endl;
    }
}; 

class Bus {
private: 
    std::queue<LRU_Cache*> io_dram_request;
    int waiting_io; 
    int* global_cycle;
    Monitor* monitor;
public: 
    Bus(int* global_cycle, Monitor* monitor): global_cycle(global_cycle), monitor(monitor) {
        waiting_io = -1;
    }
    void update_state();
    void request(LRU_Cache* cache, int data, int type);
}; 

class LRU_Cache {
private: 

    // LRU_Cache data member
    class Entry {
    public: 
        int address;
        std::vector<int> words;
        bool dirty;
        Entry() {}
        Entry(int address, std::vector<int> words, bool dirty): address(address), words(words), dirty(dirty) {}
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
        bool dirty = evict_entry.dirty;
        int address = evict_entry.address; 
        int block_memory = address / block_size; 
        int tag = block_memory / number_sets; 
        
        entry_location.erase(block_memory);
        cache_sets[index].erase(--cache_sets[index].end());
        if (dirty) {
            store_words_to_ram();
        }
        return dirty;
    }
    void store_words_to_ram() {
        n_waiting_io++; 
        if (n_waiting_io == 1) {
            bus->request(this, block_size, 0);
        }
    }
    std::vector<int> load_words_from_ram(int address) {
        int index = (int)(address / block_size) % number_sets; 
        // return vector have size = block size to represent getting
        if (cache_sets[index].size() == associativity) {
            bool check_write_back = evict(index);
        }
        n_waiting_io++; 
        if (n_waiting_io == 1) {
            bus->request(this, block_size, 0);
        }
        return std::vector<int>(block_size / word_size, 0);
    }
    std::pair<int, bool> get(int address);
    void put(int address, int word);
    void notify_finish_io();
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
        //std::string filename = "./cache_accesses_1000_0.data";
        //std::string filename = "./dram_accesses_1000_0.data";
        //std::cout << filename <<"check" << "\n";
        //exit(0);
        fin = std::ifstream(filename);
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
        // if (waiting_io) {
        //     std::cout << "printout waiting io " <<"\n";
        // }
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
        int address = hex_to_dec(address_string);
        //cnt++; 
        //std::cout << cnt << " " << type <<" "<<address<< "\n";
        if (type == 0) { 
            waiting_io = true;
            auto result = cache->get(address); 
            monitor->ls_ins[id]++;
            monitor->distribution++;
        } else if (type == 1) {
            waiting_io = true;
            cache->put(address, base);
            monitor->ls_ins[id]++;
            monitor->distribution++;
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
        bus = new Bus(global_cycle, monitor);
        cores = std::vector<Core*>(n_cores); 
        for (int i = 0; i < n_cores; i++) {
            cores[i] = new Core(i, input_file, global_cycle, monitor, bus);
        }
    }
    void run() {
        while (1) {
            bus->update_state();
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
            //std::cout <<*global_cycle<<"\n";
        }
        monitor->print_statistics();
    }
}; 

void Bus::update_state() {
    if (*global_cycle == waiting_io) {
        io_dram_request.front()->notify_finish_io(); 
        io_dram_request.pop();
    } 
    if (*global_cycle >= waiting_io && io_dram_request.size()) {
        waiting_io = *global_cycle + ram_access;
    }
}

void Bus::request(LRU_Cache* cache, int data, int type) {
    if (type == 0) { // load/store ram 
        io_dram_request.push(cache);
        monitor->bus_data_traffic += data;
    } else {
    }
}

std::pair<int, bool> LRU_Cache::get(int address) {
    int block_memory = address / block_size; 
    int tag = block_memory / number_sets; 
    int index = block_memory % number_sets; 
    int offset = (address % block_size) / word_size;
    
    if (entry_location.count(block_memory)) {
        monitor->hit_miss_cnt[core->get_id()].first++;
        reorder(index, block_memory);
    } else {
        // std::cout << "printout get" << address << " " << block_memory<<" "<<index<<"\n";
        monitor->hit_miss_cnt[core->get_id()].second++;
        auto words = load_words_from_ram(address);
        cache_sets[index].insert(cache_sets[index].begin(), Entry(block_memory * block_size, words, false));
        entry_location[block_memory] = cache_sets[index].begin();
    }
    if (!n_waiting_io) {
        core->finish_io();
    }
    // std::cout << "printout get end " << block_memory<<" "<<index<<"\n";
    auto p = std::make_pair(cache_sets[index].begin()->words[offset], true);
    // std::cout << "printout get end " << block_memory<<" "<<index<<" "<<n_waiting_io<<"\n";
    return p;
}
void LRU_Cache::put(int address, int word) {
    int block_memory = address / block_size; 
    int tag = block_memory / number_sets; 
    int index = block_memory % number_sets; 
    int offset = (address % block_size) / word_size;
    // check if cache hit or miss 
    if (entry_location.count(block_memory)) {
        // cache hit 
        monitor->hit_miss_cnt[core->get_id()].first++;
        reorder(index, block_memory);
        cache_sets[index].begin()->words[offset] = word;
        cache_sets[index].begin()->dirty = true;
    } else {
        // cache miss
        // check if the corresponding list is full or not 
        monitor->hit_miss_cnt[core->get_id()].second++;
        auto words = load_words_from_ram(address); 
        words[offset] = word;
        cache_sets[index].insert(cache_sets[index].begin(), Entry(block_memory * block_size, words, true));
        entry_location[block_memory] = cache_sets[index].begin();
        // std::cout << "printout" << address <<" "<<word << " " << block_memory<<" "<<index<<"\n";
    }
    if (!n_waiting_io) {
        core->finish_io();
    }
}
void LRU_Cache::notify_finish_io() {
    n_waiting_io--; 
    if (n_waiting_io) {
        bus->request(this, block_size, 0);
    } else {
        core->finish_io();
    }
}


int main(int argc, char* argv[]) {
    protocol = argv[1]; 
    input_file = argv[2]; 
    cache_size = std::stoi(std::string(argv[3])); 
    associativity = std::stoi(std::string(argv[4])); 
    block_size = std::stoi(std::string(argv[5]));
    int n_cores = 1; 
    Operating_System operating_system(n_cores); 
    operating_system.run();
}