// race_condition_solucion.cpp
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <mutex>  // std::mutex, std::lock_guard

using namespace std;

// Inventario: 10 productos
const int NUM_PRODUCTS = 10;
const int INITIAL_STOCK = 100;
int stock[NUM_PRODUCTS];

// Un mutex por producto
std::mutex product_mutex[NUM_PRODUCTS];

struct Operation {
    bool is_sell;      // true = vender, false = reabastecer
    int product_id;
    int quantity;
};

void random_sleep(int max_ms = 10) {
    thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, max_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
}

// ------- FUNCIONES CON SINCRONIZACION POR MUTEX -------

// SECCION CRITICA PROTEGIDA POR MUTEX:
// Se restringe a un solo hilo por producto a la vez.
void vender(int product_id, int quantity) {
    std::lock_guard<std::mutex> lock(product_mutex[product_id]);  // entrar a la sección crítica

    int current = stock[product_id];              // <-- sección crítica protegida
    random_sleep();
    stock[product_id] = current - quantity;       // <-- sección crítica protegida

    // mutex se libera automáticamente al salir de 'lock'
}

void reabastecer(int product_id, int quantity) {
    std::lock_guard<std::mutex> lock(product_mutex[product_id]);

    int current = stock[product_id];
    random_sleep();
    stock[product_id] = current + quantity;

    // mutex se libera automáticamente al salir de 'lock'
}

void run_single_simulation(int run_id) {
    // Inicializar stock
    for (int i = 0; i < NUM_PRODUCTS; ++i) {
        stock[i] = INITIAL_STOCK;
    }

    // Definir operaciones (igual que en la versión con problema)
    vector<Operation> ops(20);

    ops[0] = { true,  0, 10 };
    ops[1] = { true,  1, 15 };
    ops[2] = { true,  2, 20 };
    ops[3] = { true,  3, 5 };
    ops[4] = { true,  4, 25 };

    ops[5] = { false, 0, 30 };
    ops[6] = { false, 1, 20 };
    ops[7] = { false, 2, 40 };
    ops[8] = { false, 3, 10 };
    ops[9] = { false, 4, 35 };

    ops[10] = { true,  5, 15 };
    ops[11] = { true,  6, 20 };
    ops[12] = { true,  7, 10 };
    ops[13] = { true,  8, 25 };
    ops[14] = { true,  9, 15 };

    ops[15] = { false, 5, 25 };
    ops[16] = { false, 6, 30 };
    ops[17] = { false, 7, 15 };
    ops[18] = { false, 8, 40 };
    ops[19] = { false, 9, 20 };

    // Crear 20 threads
    vector<thread> threads;
    threads.reserve(20);

    for (int i = 0; i < 20; ++i) {
        threads.emplace_back([i, &ops]() {
            random_sleep();
            if (ops[i].is_sell) {
                vender(ops[i].product_id, ops[i].quantity);
            }
            else {
                reabastecer(ops[i].product_id, ops[i].quantity);
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    int expected0 = INITIAL_STOCK - 10 + 30;  // 120
    int expected5 = INITIAL_STOCK - 15 + 25;  // 110

    bool ok0 = (stock[0] == expected0);
    bool ok5 = (stock[5] == expected5);
    bool all_ok = ok0 && ok5;

    std::cout << "SIN RC #" << run_id << " -> "
        << "Stock[0]=" << stock[0] << " (exp " << expected0 << "), "
        << "Stock[5]=" << stock[5] << " (exp " << expected5 << ")"
        << "  => " << (all_ok ? "CORRECTO" : "INCORRECTO") << "\n";
}

int main() {
    std::cout << "===== VERSION SIN RACE CONDITION (CON MUTEX) =====\n";

    // No es necesario inicializar nada para los mutex (a diferencia de los semáforos)

    // Ejecutar 10 veces
    for (int i = 1; i <= 10; ++i) {
        run_single_simulation(i);
    }

    // No es necesario destruir nada (los mutex se manejan automáticamente)

    std::cout << "======================================================\n";
    return 0;
}
