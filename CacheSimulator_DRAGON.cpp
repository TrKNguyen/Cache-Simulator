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

// Dragon Protocol States
// NOTE: Dragon has NO Invalid state!
// If block not present in cache, it's simply not in entry_location map
enum Dragon {
    E,  // Exclusive, clean (only this cache + memory)
    Sc, // Shared, clean, not owner
    Sm, // Shared, modified, owner (memory is stale)
    M   // Modified or Dirty (dirty, private)
};

struct Monitor {
    // Cache calculate
    int overall_cyc = 0;
    std::vector<int> compute_cyc;
    std::vector<int> num_loads;    // Track loads separately
    std::vector<int> num_stores;   // Track stores separately
    std::vector<int> idle_cyc;
    std::vector<int> execution_cyc; // Per-core execution cycles
    std::vector<std::pair<int, int>> hit_miss_cnt;
    int bus_data_traffic = 0;
    int bus_invalidate_update_cnt = 0;  // For Dragon: counts updates

    int private_data_accesses = 0;  // Access to M or E state
    int shared_data_accesses = 0;   // Access to Sc or Sm state

    int num_cores;
    Monitor(int num_cores): num_cores(num_cores) {
        compute_cyc = std::vector<int>(num_cores, 0);
        num_loads = std::vector<int>(num_cores, 0);
        num_stores = std::vector<int>(num_cores, 0);
        idle_cyc = std::vector<int>(num_cores, 0);
        execution_cyc = std::vector<int>(num_cores, 0);
        hit_miss_cnt = std::vector<std::pair<int, int>>(num_cores, {0, 0});
    }
    void print_statistics() const {
        std::cout << "\n========================================" << std::endl;
        std::cout << "        SIMULATION RESULTS (DRAGON)" << std::endl;
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

        // 3. Per-core execution cycles
        std::cout << "3. Per-Core Execution Cycles:" << std::endl;
        for (int i = 0; i < num_cores; i++) {
            std::cout << "   Core " << i << ": " << execution_cyc[i] << std::endl;
        }
        std::cout << std::endl;

        // 4. Load/store instructions per core
        std::cout << "4. Load/Store Instructions:" << std::endl;
        for (int i = 0; i < num_cores; i++) {
            std::cout << "   Core " << i << ": Loads=" << num_loads[i]
                      << ", Stores=" << num_stores[i] << std::endl;
        }
        std::cout << std::endl;

        // 5. Idle cycles per core
        std::cout << "5. Idle Cycles:" << std::endl;
        for (int i = 0; i < num_cores; i++) {
            std::cout << "   Core " << i << ": " << idle_cyc[i] << std::endl;
        }
        std::cout << std::endl;

        // 6. Cache hit/miss counts per core
        std::cout << "6. Cache Statistics:" << std::endl;
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

        // 7. Bus data traffic
        std::cout << "7. Bus Data Traffic: " << bus_data_traffic << " bytes" << std::endl;
        std::cout << std::endl;

        // 8. Updates on bus (Dragon uses updates, not invalidations)
        std::cout << "8. Updates on Bus: "
                  << bus_invalidate_update_cnt << std::endl;
        std::cout << std::endl;

        // 9. Private vs shared data distribution
        std::cout << "9. Data Access Distribution:" << std::endl;
        std::cout << "   Private: " << private_data_accesses << std::endl;
        std::cout << "   Shared:  " << shared_data_accesses << std::endl;

        std::cout << "\n========================================\n" << std::endl;
    }
};

// Dragon Protocol Bus Commands
enum Cmd {
    BusRd,    // Read block from memory/another cache
    BusUpd,   // Broadcast updated word (update protocol)
    FlushWB   // Writeback to memory
};

struct BusTxn {
    Cmd cmd;
    int address;  // For BusUpd: word address; for BusRd/FlushWB: block address
    int src_core;
    int remaining_cycles;
    int supplied_by_cache = 0; // 0: none, 1: E/Sc supplies, 2: Sm/M supplies (dirty)
    int word_value = 0;  // For BusUpd: the actual word value being updated

    int supplier_core = -1;
    int bytes_on_bus = 0;
    int num_sharers = 0;  // Number of caches that have this block (for S/S' detection)

    BusTxn(Cmd cmd, int address, int src_core, int remaining_cycles, bool supplied_by_cache, int supplier_core, int bytes_on_bus) {
        this->cmd = cmd;
        this->address = address;
        this->src_core = src_core;
        this->remaining_cycles = remaining_cycles;
        this->supplied_by_cache = supplied_by_cache;
        this->supplier_core = supplier_core;
        this->bytes_on_bus = bytes_on_bus;
        this->num_sharers = 0;
        this->word_value = 0;
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
    void request(Cmd cmd, int address, int src_core, int block_size, int word_value = 0);
};

class LRU_Cache {
private:

    // LRU_Cache data member

    class Entry {
    public:
        int address;
        std::vector<int> words;
        Dragon state;

        Entry() {}
        Entry(int address, std::vector<int> words, Dragon state): address(address), words(words), state(state) {}
    };
    class Request {
    public:
        Cmd cmd;
        int address;
        int id;
        int block_size;
        bool is_write;  // Track if this is for a write operation
        int word_value;  // For BusUpd: the word value to write
        Request(Cmd cmd, int address, int id, int block_size, bool is_write = false, int word_value = 0):
            cmd(cmd), address(address), id(id), block_size(block_size), is_write(is_write), word_value(word_value) {}
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

    // For cache hits that take 1 cycle
    int hit_complete_at_cycle;
    bool pending_hit_completion;
public:
    LRU_Cache(Core* core, int cache_size, int associativity, int block_size, int* global_cycle, Monitor* monitor, Bus* bus): core(core), cache_size(cache_size), associativity(associativity), block_size(block_size), global_cycle(global_cycle), monitor(monitor), bus(bus)  {
        number_sets = cache_size / (associativity * block_size);
        cache_sets = std::vector<std::list<Entry>>(number_sets, std::list<Entry>());
        n_waiting_io = 0;
        pending_hit_completion = false;
        hit_complete_at_cycle = -1;
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
        // In Dragon: writeback needed for M or Sm states
        if (state == Dragon::M || state == Dragon::Sm) {
            store_words_to_ram(address);
        }
        return (state == Dragon::M || state == Dragon::Sm);
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
    bool snoop(Cmd cmd, int address, std::optional<BusTxn>& active);  // Returns true if this cache has the block
    std::pair<int, bool> get(int address);
    void put(int address, int word);
    void notify_finish_io();
    void tick();  // Process cache hit delays

    void update_entry_state(int address, Dragon state);
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
    int pending_compute_cycles = 0;  // Compute cycles since last load/store
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
            // Load: count pending compute cycles (they were BETWEEN operations)
            monitor->compute_cyc[id] += pending_compute_cycles;
            pending_compute_cycles = 0;

            waiting_io = true;
            auto result = cache->get(address);
            monitor->num_loads[id]++;
        } else if (type == 1) {
            // Store: count pending compute cycles (they were BETWEEN operations)
            monitor->compute_cyc[id] += pending_compute_cycles;
            pending_compute_cycles = 0;

            waiting_io = true;
            cache->put(address, base);
            monitor->num_stores[id]++;
        } else {
            // Compute: accumulate but don't count yet (might be after last load/store)
            int cal_cycles = address;
            waiting_cal = *global_cycle + cal_cycles;
            pending_compute_cycles += cal_cycles;
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
            // Tick caches to complete any pending hits from previous cycle
            for (int i = 0; i < n_cores; i++) {
                cores[i]->get_cache()->tick();
            }
            bus->tick();
            bool check_all_finish = true;
            for (int i = 0; i < n_cores; i++) {
                bool still_running = cores[i]->execute_next_instruction();
                // Record execution cycle when core finishes (at end of current cycle)
                if (!still_running && monitor->execution_cyc[i] == 0) {
                    monitor->execution_cyc[i] = *global_cycle;
                }
                check_all_finish = (check_all_finish && !still_running);
            }
            if (check_all_finish) {
                break;
            }
            (*global_cycle)++;
        }
        // Overall cycle is the maximum execution cycle
        int max_exec = 0;
        for (int i = 0; i < n_cores; i++) {
            monitor->compute_cyc[i] += (*cores[i]).pending_compute_cycles;
            if (monitor->execution_cyc[i] > max_exec) {
                max_exec = monitor->execution_cyc[i];
            }
        }
        monitor->overall_cyc = max_exec;
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


bool LRU_Cache::snoop(Cmd cmd, int address, std::optional<BusTxn>& active) {
    int block_memory = address / block_size;
    int tag = block_memory / number_sets;
    if (!entry_location.count(block_memory)) {
        return false;  // Don't have this block
    }
    Entry& entry = *entry_location[block_memory];

    if (cmd == Cmd::BusRd) {
        // Dragon BusRd snooping
        if (entry.state == Dragon::E) {
            // E → Sc: supply data, become shared clean
            (*active).supplier_core = core->get_id();
            (*active).supplied_by_cache = 1;  // Clean supply
            entry.state = Dragon::Sc;
            return true;  // We have the block (assert shared signal)
        } else if (entry.state == Dragon::Sc) {
            // Sc → stay Sc: assert shared, don't supply (not owner)
            return true;  // We have the block (assert shared signal)
        } else if (entry.state == Dragon::Sm) {
            // Sm → stay Sm: Flush (supply data), remain owner
            (*active).supplier_core = core->get_id();
            (*active).supplied_by_cache = 2;  // Dirty supply
            // Stay in Sm (still owner)
            return true;  // We have the block (assert shared signal)
        } else if (entry.state == Dragon::M) {
            // M → Sm: Flush (supply data), become shared owner
            (*active).supplier_core = core->get_id();
            (*active).supplied_by_cache = 2;  // Dirty supply
            entry.state = Dragon::Sm;
            return true;  // We have the block (assert shared signal)
        }
    } else if (cmd == Cmd::BusUpd) {
        // Dragon BusUpd snooping - update word in our copy
        int offset = (address % block_size) / word_size;

        if (entry.state == Dragon::Sc) {
            // Sc → stay Sc: update word
            entry.words[offset] = (*active).word_value;
            return true;  // We have the block and updated it
        } else if (entry.state == Dragon::Sm) {
            // Sm → Sc: update word, lose ownership
            entry.words[offset] = (*active).word_value;
            entry.state = Dragon::Sc;
            return true;  // We have the block and updated it
        } else if (entry.state == Dragon::E) {
            // E → Sc: update word (rare case)
            entry.words[offset] = (*active).word_value;
            entry.state = Dragon::Sc;
            return true;  // We have the block and updated it
        }
        // If in M state, shouldn't receive BusUpd (we're private)
    } else if (cmd == Cmd::FlushWB) {
        // No state change on writeback
    }

    return false;
}

std::pair<int, bool> LRU_Cache::get(int address) {
    int block_memory = address / block_size;
    int tag = block_memory / number_sets;
    int index = block_memory % number_sets;
    int offset = (address % block_size) / word_size;

    if (entry_location.count(block_memory)) {
        // Dragon: Block present in cache - MUST be in valid state (E, Sc, Sm, or M)
        Entry& entry = *entry_location[block_memory];

        if (entry.state == Dragon::M || entry.state == Dragon::E) {
            // Cache hit in M or E state - private data access
            monitor->private_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].first++;
            reorder(index, block_memory);
            auto p = std::make_pair(cache_sets[index].begin()->words[offset], true);
            // Cache hit takes 1 cycle - schedule completion
            pending_hit_completion = true;
            hit_complete_at_cycle = *global_cycle + 1;
            return p;
        } else {
            // entry.state == Dragon::Sc || entry.state == Dragon::Sm
            // Cache hit in Sc or Sm state - shared data access
            monitor->shared_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].first++;
            reorder(index, block_memory);
            auto p = std::make_pair(cache_sets[index].begin()->words[offset], true);
            // Cache hit takes 1 cycle - schedule completion
            pending_hit_completion = true;
            hit_complete_at_cycle = *global_cycle + 1;
            return p;
        }
    } else {
        // Cache miss - block not present in cache
        // Dragon: No Invalid state - block simply doesn't exist yet
        monitor->hit_miss_cnt[core->get_id()].second++;
        auto words = load_words_from_ram(address);
        // Create entry optimistically in E state, will be updated to Sc if sharers detected
        cache_sets[index].insert(cache_sets[index].begin(), Entry(block_memory * block_size, words, Dragon::E));
        entry_location[block_memory] = cache_sets[index].begin();

        // For misses, return dummy value - will wait for bus transaction
        auto p = std::make_pair(0, true);
        return p;
    }
}

void LRU_Cache::put(int address, int word) {
    int block_memory = address / block_size;
    int tag = block_memory / number_sets;
    int index = block_memory % number_sets;
    int offset = (address % block_size) / word_size;

    if (entry_location.count(block_memory)) {
        // Dragon: Block present in cache - MUST be in valid state (E, Sc, Sm, or M)
        Entry& entry = *entry_location[block_memory];

        if (entry.state == Dragon::M) {
            // Cache hit in M state - already have exclusive ownership
            monitor->private_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].first++;
            reorder(index, block_memory);
            cache_sets[index].begin()->words[offset] = word;
            // Stay in M state, takes 1 cycle
            pending_hit_completion = true;
            hit_complete_at_cycle = *global_cycle + 1;
            return;
        } else if (entry.state == Dragon::E) {
            // Cache hit in E state - have exclusive ownership
            monitor->private_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].first++;
            reorder(index, block_memory);
            cache_sets[index].begin()->words[offset] = word;
            cache_sets[index].begin()->state = Dragon::M;
            // Takes 1 cycle
            pending_hit_completion = true;
            hit_complete_at_cycle = *global_cycle + 1;
            return;
        } else if (entry.state == Dragon::Sc) {
            // Write to Sc state - need to send BusUpd (update others)
            monitor->shared_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].second++;  // Counted as miss (needs bus transaction)
            n_waiting_io++;
            requests.push(Request(Cmd::BusUpd, address, this->core->get_id(), word_size, false, word));
            if (requests.size() == 1) {
                auto& request = requests.front();
                bus->request(request.cmd, request.address, request.id, word_size, request.word_value);
                requests.pop();
            }
            reorder(index, block_memory);
            cache_sets[index].begin()->words[offset] = word;
            // State will be updated by bus to Sm or M based on S/S'
        } else {
            // entry.state == Dragon::Sm
            // Write to Sm state - send BusUpd to update sharers
            monitor->shared_data_accesses++;
            monitor->hit_miss_cnt[core->get_id()].second++;  // Counted as miss (needs bus transaction)
            n_waiting_io++;
            requests.push(Request(Cmd::BusUpd, address, this->core->get_id(), word_size, false, word));
            if (requests.size() == 1) {
                auto& request = requests.front();
                bus->request(request.cmd, request.address, request.id, word_size, request.word_value);
                requests.pop();
            }
            reorder(index, block_memory);
            cache_sets[index].begin()->words[offset] = word;
            // Stay in Sm (already owner)
        }
    } else {
        // Cache miss - block not present in cache
        // Dragon: No Invalid state - block simply doesn't exist yet
        // Check if eviction needed
        if (cache_sets[index].size() >= (size_t)associativity) {
            evict(index);
        }

        monitor->hit_miss_cnt[core->get_id()].second++;
        n_waiting_io++;

        // Create entry with initial words, optimistically in E state
        auto words = std::vector<int>(block_size / word_size, 0);
        words[offset] = word;
        cache_sets[index].insert(cache_sets[index].begin(), Entry(block_memory * block_size, words, Dragon::E));
        entry_location[block_memory] = cache_sets[index].begin();

        // For write miss in Dragon:
        // 1. First get the block with BusRd (marked as write)
        // 2. Then conditionally BusUpd if sharers detected (checked in notify_finish_io)
        requests.push(Request(Cmd::BusRd, address, this->core->get_id(), block_size, true, word));
        requests.push(Request(Cmd::BusUpd, address, this->core->get_id(), word_size, true, word));
        if (requests.size() == 2) {
            auto& request = requests.front();
            bus->request(request.cmd, request.address, request.id, request.block_size);
            requests.pop();
        }
    }
}

void LRU_Cache::notify_finish_io() {
    n_waiting_io--;
    assert(n_waiting_io >= 0);
    if (requests.size()) {
        auto& request = requests.front();

        // For Dragon: if this is a BusUpd following a write BusRd, check if we need to send it
        if (request.cmd == Cmd::BusUpd && request.is_write) {
            // Check current state after BusRd completed
            int block_memory = request.address / block_size;
            if (entry_location.count(block_memory)) {
                Entry& entry = *entry_location[block_memory];
                if (entry.state == Dragon::E) {
                    // No sharers - transition E→M without BusUpd
                    entry.state = Dragon::M;
                    requests.pop();
                    // Don't send BusUpd to bus, skip to next request
                    if (requests.size()) {
                        auto& next_request = requests.front();
                        bus->request(next_request.cmd, next_request.address, next_request.id, next_request.block_size, next_request.word_value);
                        requests.pop();
                    }
                    if (n_waiting_io <= 0) {
                        core->finish_io();
                    }
                    return;
                }
                // If Sc, we need to send BusUpd to become Sm
            }
        }

        bus->request(request.cmd, request.address, request.id, request.block_size, request.word_value);
        requests.pop();
    }
    if (n_waiting_io <= 0) {
        core->finish_io();
    }
}

void LRU_Cache::tick() {
    // Check if cache hit delay has elapsed
    if (pending_hit_completion && *global_cycle >= hit_complete_at_cycle) {
        pending_hit_completion = false;
        core->finish_io();
    }
}

void LRU_Cache::update_entry_state(int address, Dragon state) {
    int block_memory = address / block_size;
    if (entry_location.find(block_memory) == entry_location.end()) {
        return;  // Entry doesn't exist, can't update
    }
    Entry& entry = *entry_location[block_memory];

    // Dragon: No Invalid state
    // Entries created optimistically in E, then potentially downgraded
    // Track private vs shared transitions for metrics
    // (Note: initial accesses already tracked in get()/put(), this handles bus updates)

    entry.state = state;
}

// Bus implementations

void Bus::tick() {
    if (active == std::nullopt || (*active).remaining_cycles == 1) {
        // handle the finish-executed bus transaction
        if (active != std::nullopt) {
            (*monitor).bus_data_traffic += (*active).bytes_on_bus;

            // Count BusUpd as updates (write-update protocol)
            if ((*active).cmd == Cmd::BusUpd) {
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

            // Count the number of caches that have this block
            int num_sharers = 0;
            for (auto& core: cores) {
                if (core->get_id() == (*active).src_core) {
                    continue;
                }
                bool has_block = core->get_cache()->snoop((*active).cmd, (*active).address, active);
                if (has_block) {
                    num_sharers++;
                }
            }
            (*active).num_sharers = num_sharers;

            auto cmd = (*active).cmd;
            if (cmd == Cmd::BusRd) {
                // Dragon BusRd timing and state updates

                if ((*active).supplied_by_cache == 2) {
                    // Supplied by another cache Sm/M (dirty)
                    (*active).bytes_on_bus = 2 * block_size;  // Cache-to-cache transfer
                    (*active).remaining_cycles = 2 * block_size / word_size;

                    // Destination goes to Sc (shared clean, not owner)
                    for (auto& core: cores) {
                        if (core->get_id() == (*active).src_core) {
                            core->get_cache()->update_entry_state((*active).address, Dragon::Sc);
                            break;
                        }
                    }
                } else if ((*active).supplied_by_cache == 1) {
                    // Supplied by another cache E (clean)
                    (*active).bytes_on_bus = 2 * block_size;  // Cache-to-cache transfer
                    (*active).remaining_cycles = 2 * block_size / word_size;

                    // Destination goes to Sc (shared clean)
                    for (auto& core: cores) {
                        if (core->get_id() == (*active).src_core) {
                            core->get_cache()->update_entry_state((*active).address, Dragon::Sc);
                            break;
                        }
                    }
                } else {
                    // Not supplied by cache - memory supplies
                    (*active).bytes_on_bus = block_size;
                    (*active).remaining_cycles = 100;

                    // Destination goes to E (exclusive, no other sharers)
                    for (auto& core: cores) {
                        if (core->get_id() == (*active).src_core) {
                            core->get_cache()->update_entry_state((*active).address, Dragon::E);
                            break;
                        }
                    }
                }
            } else if (cmd == Cmd::BusUpd) {
                // Dragon BusUpd timing and state updates
                // BusUpd sends ONE WORD only
                (*active).bytes_on_bus = word_size;  // 4 bytes
                (*active).remaining_cycles = 2;  // 2 cycles per problem statement

                // Update destination state based on S/S' (shared signal)
                // If num_sharers > 0: others still have it (S) → Sm
                // If num_sharers == 0: we're alone (S') → M
                for (auto& core: cores) {
                    if (core->get_id() == (*active).src_core) {
                        if (num_sharers > 0) {
                            // S: others have it, we become Sm (shared owner)
                            core->get_cache()->update_entry_state((*active).address, Dragon::Sm);
                        } else {
                            // S': no others, we become M (private)
                            core->get_cache()->update_entry_state((*active).address, Dragon::M);
                        }
                        break;
                    }
                }
            } else if (cmd == Cmd::FlushWB) {
                // Writeback to memory
                (*active).remaining_cycles = 100;
                (*active).bytes_on_bus = block_size;
            }
        }
    } else {
        (*active).remaining_cycles--;
    }
}

void Bus::request(Cmd cmd, int address, int src_core, int block_size, int word_value) {
    if (cmd == Cmd::BusRd) {
        pending.push(BusTxn(cmd, address, src_core, -1, false, -1, -1));
    } else if (cmd == Cmd::BusUpd) {
        BusTxn txn(cmd, address, src_core, -1, false, -1, -1);
        txn.word_value = word_value;  // Store the word value for BusUpd
        pending.push(txn);
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
    std::cout << "  Protocol: DRAGON" << std::endl;
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