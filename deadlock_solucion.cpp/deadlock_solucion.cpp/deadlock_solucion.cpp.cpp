// deadlock_solucion.cpp
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
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <algorithm>
using namespace std;
using Clock = chrono::steady_clock;
using ms = chrono::milliseconds;

struct Account {
    int id;
    long long balance;
    std::mutex mtx;

    Account(const Account&) = delete;
    Account& operator=(const Account&) = delete;

    Account(Account&& other) noexcept
        : id(other.id), balance(other.balance) {
    }

    Account& operator=(Account&& other) noexcept {
        if (this != &other) {
            id = other.id;
            balance = other.balance;
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

void do_transfer_nodl(int thread_no) {
    for (auto& t : thread_transfers[thread_no - 1]) {
        int a = t.from, b = t.to;
        int low = min(a, b), high = max(a, b);
        log_event(thread_no, "Attempting ordered lock low=" + to_string(low) + " high=" + to_string(high));
        unique_lock<mutex> lk1(accounts[low].mtx, std::defer_lock);
        unique_lock<mutex> lk2(accounts[high].mtx, std::defer_lock);
        std::lock(lk1, lk2);
        log_event(thread_no, "Acquired both locks (" + to_string(low) + "," + to_string(high) + ")");

        if (accounts[a].balance >= t.amount) {
            accounts[a].balance -= t.amount;
            accounts[b].balance += t.amount;
            transfers_completed.fetch_add(1);
            log_event(thread_no, "Transfer " + to_string(a) + "->" + to_string(b) + " $" + to_string(t.amount) + " SUCCESS");
        }
        else {
            log_event(thread_no, "Transfer " + to_string(a) + "->" + to_string(b) + " $" + to_string(t.amount) + " FAILED (insufficient)");
        }

        log_event(thread_no, "Releasing locks (" + to_string(low) + "," + to_string(high) + ")");
        this_thread::sleep_for(chrono::milliseconds(20));
    }
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
        threads.emplace_back(do_transfer_nodl, i);
    }
    for (auto& th : threads) if (th.joinable()) th.join();
    auto end = Clock::now();
    auto elapsed = chrono::duration_cast<ms>(end - start).count();

    cout << "\n== Summary ==\n";
    cout << "Transfers completed: " << transfers_completed.load() << " / 30\n";
    cout << "Execution time (ms): " << elapsed << "\n";
    cout << "Final balances:\n";
    for (auto& a : accounts) cout << "Account " << a.id << " = $" << a.balance << "\n";

    return 0;
}
