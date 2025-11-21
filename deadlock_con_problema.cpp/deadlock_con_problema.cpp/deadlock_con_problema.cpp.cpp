// deadlock_con_problema.cpp
#include <new>
#include <utility>

#if !defined(__cpp_lib_construct_at)
namespace std {
    template <class T, class... Args>
    constexpr T* construct_at(T* p, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        return ::new (const_cast<void*>(static_cast<const volatile void*>(p)))
            T(std::forward<Args>(args)...);
    }
}
#endif
#include <iostream>
#include <memory>
#include <iomanip>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <vector>
#include <ctime>
using namespace std;
using Clock = chrono::steady_clock;
using ms = chrono::milliseconds;

struct Account {
    int id;
    long long balance;
    std::mutex mtx;

    // Evitar copia (por std::mutex)
    Account(const Account&) = delete;
    Account& operator=(const Account&) = delete;

    // Permitir movimiento (necesario para std::vector)
    Account(Account&& other) noexcept
        : id(other.id), balance(other.balance) {
        // No se mueve el mutex (se construye nuevo)
    }

    Account& operator=(Account&& other) noexcept {
        if (this != &other) {
            id = other.id;
            balance = other.balance;
            // El mutex no se mueve
        }
        return *this;
    }

    Account(int i, long long b) : id(i), balance(b) {}
    Account() = default;
};


struct Transfer { int from, to, amount; };

vector<Account> accounts;
vector<vector<Transfer>> thread_transfers;
atomic<int> transfers_completed{ 0 };
mutex log_mtx;

void log_event(int thread_no, const string& msg) {
    lock_guard<mutex> lg(log_mtx);
    auto now = chrono::system_clock::now();
    time_t tt = chrono::system_clock::to_time_t(now);

    struct tm timeinfo;
#ifdef _MSC_VER
    localtime_s(&timeinfo, &tt);
#else
    localtime_r(&tt, &timeinfo);
#endif

    auto ms_part = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()) % 1000;
    cout << "[" << put_time(&timeinfo, "%T") << "." << setfill('0') << setw(3) << ms_part.count()
        << "] Thread " << thread_no << " - " << msg << "\n";
}

// thread states used for deadlock inspection
struct ThreadState {
    int holding = -1; // account id currently locked (if any)
    int waiting_for = -1; // account id it's trying to lock
    bool finished = false;
};
vector<ThreadState> tstates(11); // 1..10

void do_transfer_deadlock(int thread_no) {
    for (auto& t : thread_transfers[thread_no - 1]) {
        log_event(thread_no, "Attempting lock on origin " + to_string(t.from));
        tstates[thread_no].waiting_for = t.from;
        accounts[t.from].mtx.lock();
        tstates[thread_no].holding = t.from;
        tstates[thread_no].waiting_for = -1;
        log_event(thread_no, "Acquired lock on origin " + to_string(t.from));

        this_thread::sleep_for(chrono::milliseconds(50));

        log_event(thread_no, "Attempting lock on dest " + to_string(t.to));
        tstates[thread_no].waiting_for = t.to;
        accounts[t.to].mtx.lock(); // <-- potential deadlock point
        tstates[thread_no].waiting_for = -1;
        log_event(thread_no, "Acquired lock on dest " + to_string(t.to));

        if (accounts[t.from].balance >= t.amount) {
            accounts[t.from].balance -= t.amount;
            accounts[t.to].balance += t.amount;
            transfers_completed.fetch_add(1);
            log_event(thread_no, "Transfer " + to_string(t.from) + "->" + to_string(t.to) + " $" + to_string(t.amount) + " SUCCESS");
        }
        else {
            log_event(thread_no, "Transfer " + to_string(t.from) + "->" + to_string(t.to) + " $" + to_string(t.amount) + " FAILED (insufficient)");
        }

        accounts[t.to].mtx.unlock();
        log_event(thread_no, "Released lock on dest " + to_string(t.to));
        tstates[thread_no].holding = t.from;
        accounts[t.from].mtx.unlock();
        log_event(thread_no, "Released lock on origin " + to_string(t.from));
        tstates[thread_no].holding = -1;

        this_thread::sleep_for(chrono::milliseconds(20));
    }
    tstates[thread_no].finished = true;
    log_event(thread_no, "Finished its transfers");
}

int main() {
    for (int i = 0; i < 5; i++) accounts.push_back({ i, (long long)1000 * (i + 1) });
    thread_transfers.resize(10);
    thread_transfers[0] = { {0,1,200},{1,2,300},{2,0,150} };
    thread_transfers[1] = { {1,0,250},{0,2,100},{2,1,200} };
    thread_transfers[2] = { {2,3,300},{3,4,400},{4,2,250} };
    thread_transfers[3] = { {3,2,350},{2,4,200},{4,3,300} };
    thread_transfers[4] = { {4,0,400},{0,3,250},{3,4,150} };
    thread_transfers[5] = { {0,4,300},{4,1,350},{1,0,200} };
    thread_transfers[6] = { {1,3,250},{3,0,300},{0,1,150} };
    thread_transfers[7] = { {2,1,200},{1,4,250},{4,2,300} };
    thread_transfers[8] = { {3,1,300},{1,2,200},{2,3,250} };
    thread_transfers[9] = { {4,3,350},{3,2,250},{2,4,200} };

    vector<thread> threads;
    auto start = Clock::now();
    for (int i = 1; i <= 10; i++) {
        threads.emplace_back(do_transfer_deadlock, i);
    }

    auto last_progress = Clock::now();
    int last_completed = transfers_completed.load();
    bool deadlock_reported = false;
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(200));
        int cur = transfers_completed.load();
        if (cur != last_completed) {
            last_completed = cur;
            last_progress = Clock::now();
        }
        else {
            auto diff = chrono::duration_cast<chrono::seconds>(Clock::now() - last_progress).count();
            if (diff >= 3 && cur < 30) {
                lock_guard<mutex> lg(log_mtx);
                cout << "\n===== DEADLOCK SUSPECTED (no progress for " << diff << "s) =====\n";
                cout << "Transfers completed: " << cur << " / 30\n";
                for (int i = 1; i <= 10; i++) {
                    cout << "Thread " << i << ": ";
                    if (tstates[i].finished) cout << "FINISHED";
                    else {
                        if (tstates[i].holding != -1) cout << "HOLDING account " << tstates[i].holding << " ";
                        if (tstates[i].waiting_for != -1) cout << "WAITING_FOR account " << tstates[i].waiting_for;
                    }
                    cout << "\n";
                }
                cout << "Account balances snapshot:\n";
                for (auto& a : accounts) cout << "Account " << a.id << " = $" << a.balance << "\n";
                cout << "=====================================================\n\n";
                deadlock_reported = true;
                break;
            }
        }
        bool all_done = true;
        for (int i = 1; i <= 10; i++) if (!tstates[i].finished) { all_done = false; break; }
        if (all_done) break;
    }

    for (auto& th : threads) {
        if (th.joinable()) {
            if (deadlock_reported) {
                if (th.get_id() != this_thread::get_id()) th.detach();
            }
            else {
                th.join();
            }
        }
    }
    auto end = Clock::now();
    auto elapsed = chrono::duration_cast<ms>(end - start).count();

    cout << "\n== Summary ==\n";
    cout << "Transfers completed: " << transfers_completed.load() << " / 30\n";
    cout << "Execution time (ms): " << elapsed << (deadlock_reported ? " (stopped due to suspected deadlock)\n" : "\n");

    cout << "Final balances (best-effort snapshot):\n";
    for (auto& a : accounts) cout << "Account " << a.id << " = $" << a.balance << "\n";

    return 0;
}
