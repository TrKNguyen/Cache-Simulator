#include <bits/stdc++.h>

struct Config {
    std::string protocol;
    std::string input_file;
    unsigned int cache_size;
    unsigned int associativity;
    unsigned int block_size;
    Config(): protocol(""), input_file(""), associativity(2), cache_size(4096), block_size(32) {}
} config;

enum Instructions: int { LOAD = 0, STORE = 1, OTH = 2 };

const int L1_HIT_LAT = 1;
const int DRAM_READ_LAT = 100;
const int DRAM_WRITE_LAT = 100;

struct CacheBlock {
    unsigned int tag{};
    bool dirty{false};
    explicit CacheBlock(unsigned int t): tag(t), dirty(false) {}
    CacheBlock() = default;
};

inline unsigned long long to_hex_ull(const std::string& s) {
    return std::stoull(s, nullptr, 16);
}

struct CacheSet {
    using key_type = unsigned int;
    using pos_type = std::list<CacheBlock>::iterator;

    std::unordered_map<key_type, pos_type> pos_cache;
    std::list<CacheBlock> cache;

    //  {evicted, was_dirty}
    std::pair<bool,bool> evict_if_needed() {
        if (cache.size() > config.associativity) {
            CacheBlock &blk = cache.front();
            bool dirty = blk.dirty;
            pos_cache.erase(blk.tag);
            cache.pop_front();
            return {true, dirty};
        }
        return {false, false};
    }

    // {hit, evicted_dirty}
    std::pair<bool,bool> load_miss_or_hit(const key_type& tag) {
        auto it = pos_cache.find(tag);
        if (it == pos_cache.end()) {
            cache.push_back(CacheBlock(tag));
            auto ev = evict_if_needed();
            pos_cache[tag] = std::prev(cache.end());
            return {false, ev.second};
        } else {
            cache.splice(cache.end(), cache, it->second);          // LRU to MRU
            pos_cache[tag] = std::prev(cache.end());
            return {true, false};
        }
    }

    // {hit, evicted_dirty}
    std::pair<bool,bool> store_miss_or_hit(const key_type& tag) {
        auto it = pos_cache.find(tag);
        if (it == pos_cache.end()) {
            cache.push_back(CacheBlock(tag));
            auto ev = evict_if_needed();
            pos_cache[tag] = std::prev(cache.end());
            return {false, ev.second};
        } else {
            cache.splice(cache.end(), cache, it->second);
            pos_cache[tag] = std::prev(cache.end());
            return {true, false};
        }
    }

    void mark_dirty(const key_type& tag) {
        auto it = pos_cache.find(tag);
        if (it != pos_cache.end()) it->second->dirty = true;
    }
};

struct L1Cache {
    std::vector<CacheSet> sets;

    L1Cache(): sets(static_cast<size_t>(std::max(1u, config.cache_size / (config.block_size * config.associativity)))) {}

    static unsigned int sets_n() {
        return std::max(1u, config.cache_size / (config.block_size * config.associativity));
    }

    static unsigned int get_tag(const unsigned int address) {
        const unsigned int sN = sets_n();
        const unsigned int block_number = address / config.block_size;
        return block_number / sN;
    }

    static unsigned int get_set_id(const unsigned int address) {
        const unsigned int sN = sets_n();
        const unsigned int block_number = address / config.block_size;
        return block_number % sN;
    }

    std::pair<bool,bool> load_access(const unsigned int address) {
        unsigned int set_id = get_set_id(address);
        unsigned int tag = get_tag(address);
        return sets[set_id].load_miss_or_hit(tag);
    }

    std::pair<bool,bool> store_access(const unsigned int address) {
        unsigned int set_id = get_set_id(address);
        unsigned int tag = get_tag(address);
        return sets[set_id].store_miss_or_hit(tag);
    }

    void mark_dirty(const unsigned int address) {
        unsigned int set_id = get_set_id(address);
        unsigned int tag = get_tag(address);
        sets[set_id].mark_dirty(tag);
    }
};

long long total_cycles;
long long compute_cycles;
long long idle_cycles;
long long loads_cnt;
long long stores_cnt;
long long hits_cnt;
long long misses_cnt;
long long dirty_writebacks;
long long bus_bytes;

void reset_counters() {
    total_cycles = 0;
    compute_cycles = 0;
    idle_cycles = 0;
    loads_cnt = stores_cnt = 0;
    hits_cnt = misses_cnt = 0;
    dirty_writebacks = 0;
    bus_bytes = 0;
}

void charge_hit() {
    total_cycles += L1_HIT_LAT;
}

void charge_miss(bool evicted_dirty) {
    total_cycles += L1_HIT_LAT + DRAM_READ_LAT;
    idle_cycles += DRAM_READ_LAT;
    bus_bytes += config.block_size;
    if (evicted_dirty) {
        total_cycles += DRAM_WRITE_LAT;
        idle_cycles += DRAM_WRITE_LAT;
        bus_bytes += config.block_size;
        ++dirty_writebacks;
    }
}

void execute(L1Cache& l1_cache) {
    reset_counters();

    int instr_type;
    std::string token;
    while (std::cin >> instr_type >> token) {
        // std::cerr << instr_type << " " << token << std::endl;
        if (instr_type == OTH) {
            unsigned long long c = to_hex_ull(token);
            total_cycles += static_cast<long long>(c);
            compute_cycles += static_cast<long long>(c);
            continue;
        }

        unsigned int addr = static_cast<unsigned int>(to_hex_ull(token));

        if (instr_type == LOAD) {
            ++loads_cnt;
            auto [hit, evd] = l1_cache.load_access(addr);
            if (hit) { 
                ++hits_cnt;
				charge_hit();
            } else { 
                ++misses_cnt;
                charge_miss(evd);
            }
        } else if (instr_type == STORE) {
            ++stores_cnt;
            auto [hit, evd] = l1_cache.store_access(addr);
            if (hit) {
                ++hits_cnt;
                charge_hit();
            } else {
                ++misses_cnt;
                charge_miss(evd);
            }
            l1_cache.mark_dirty(addr);
        }
    }

    double miss_rate = (loads_cnt + stores_cnt)
        ? static_cast<double>(misses_cnt) / static_cast<double>(loads_cnt + stores_cnt)
        : 0.0;

    std::cout << "TotalCycles: " << total_cycles << "\n";
    std::cout << "ComputeCycles: " << compute_cycles << "\n";
    std::cout << "IdleCycles: " << idle_cycles << "\n";
    std::cout << "Loads: " << loads_cnt << "\n";
    std::cout << "Stores: " << stores_cnt << "\n";
    std::cout << "Hits: " << hits_cnt << "\n";
    std::cout << "Misses: " << misses_cnt << "\n";
    std::cout.setf(std::ios::fixed);
    std::cout << std::setprecision(6) << "MissRate: " << miss_rate << "\n";
    std::cout.unsetf(std::ios::fixed);
    std::cout << "BusDataBytes: " << bus_bytes << "\n";
    std::cout << "DirtyWritebacks: " << dirty_writebacks << "\n";
    std::cout << "BlockSize: " << config.block_size << "\n";
    std::cout << "Assoc: " << config.associativity << "\n";
    std::cout << "CacheSize: " << config.cache_size << "\n";
}

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (argc < 3) {
        std::cerr << "Need protocol and file name\n";
        return 1;
    }
    config.protocol = argv[1];
    config.input_file = argv[2];
    std::cerr << argc << " " << argv[3] << std::endl;
    if (argc >= 4) config.cache_size = std::stoi(argv[3]);
    if (argc >= 5) config.associativity = std::stoi(argv[4]);
    if (argc >= 6) config.block_size = std::stoi(argv[5]);

    L1Cache l1_cache;

    std::string file_name = config.input_file + "_0.data";
    std::freopen(file_name.c_str(), "r", stdin);

    execute(l1_cache);
    return 0;
}


