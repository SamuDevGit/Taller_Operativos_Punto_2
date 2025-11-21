// starvation_solucion.cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>

using namespace std::chrono;

// Representa una tarea en la cola
struct Task {
    char type; // 'A', 'M', 'B'
    int id;    // identificador único
    steady_clock::time_point enqueue_time; // instante en que entró a la cola
};

class Simulation {
public:
    Simulation()
        : MAX_QUEUE(20),
        RUN_SECONDS(10),
        next_id(0)
    {
        // Secuencia fija de las primeras 30 tareas
        initial_sequence = {
            // 1-10
            'B','B','M','B','B','B','A','M','B','B',
            // 11-20
            'B','B','B','M','A','B','B','M','B','B',
            // 21-30
            'M','B','B','B','A','B','M','B','B','B'
        };
    }

    void run() {
        // Resetear estado compartido
        {
            std::lock_guard<std::mutex> lk(mtx_);
            queue_.clear();
        }

        processedA = 0;
        processedM = 0;
        processedB = 0;

        stop_production = false;
        stop_consumers = false;
        initial_done = false;
        next_id = 0;

        // Lanzar hilos
        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;

        for (int i = 0; i < 3; ++i) {
            consumers.emplace_back(&Simulation::consumer, this, i);
        }
        for (int i = 0; i < 5; ++i) {
            producers.emplace_back(&Simulation::producer, this, i);
        }

        std::thread monitor_thread(&Simulation::monitor, this);

        // Dejar correr 10 segundos (para la tabla)
        std::this_thread::sleep_for(std::chrono::seconds(RUN_SECONDS));

        // A los 10s: parar producción
        stop_production = true;
        cv_not_full_.notify_all();

        // Esperar a que terminen productores
        for (auto& p : producers) {
            if (p.joinable()) p.join();
        }

        // Versión SIN starvation:
        // dejamos que los consumidores sigan hasta vaciar la cola
        while (true) {
            std::unique_lock<std::mutex> lk(mtx_);
            if (queue_.empty()) break;
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Ahora sí detenemos consumidores
        stop_consumers = true;
        cv_not_empty_.notify_all();
        for (auto& c : consumers) {
            if (c.joinable()) c.join();
        }

        if (monitor_thread.joinable()) monitor_thread.join();

        // Resumen final
        int pendingTotal = 0;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pendingTotal = (int)queue_.size();
        }

        std::cout << "\n=============================\n";
        std::cout << "Resumen final (SIN starvation - con aging)\n";
        std::cout << "Tareas A procesadas: " << processedA.load() << "\n";
        std::cout << "Tareas M procesadas: " << processedM.load() << "\n";
        std::cout << "Tareas B procesadas: " << processedB.load() << "\n";
        std::cout << "Tareas en cola al final: " << pendingTotal << "\n";
        std::cout << "¿Cola vacia al terminar consumidores? "
            << (pendingTotal == 0 ? "SI" : "NO") << "\n";
        std::cout << "=============================\n\n";
    }

private:
    // Parámetros
    const size_t MAX_QUEUE;
    const int RUN_SECONDS;

    // Cola compartida
    std::deque<Task> queue_;
    std::mutex mtx_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;

    // Estado global
    std::atomic<bool> stop_production{ false };
    std::atomic<bool> stop_consumers{ false };
    std::atomic<bool> initial_done{ false };

    std::atomic<int> processedA{ 0 };
    std::atomic<int> processedM{ 0 };
    std::atomic<int> processedB{ 0 };

    std::atomic<int> next_id;

    std::vector<char> initial_sequence;

    // ----- Funciones auxiliares -----

    int base_priority(char type) const {
        // A > M > B
        switch (type) {
        case 'A': return 3;
        case 'M': return 2;
        case 'B': return 1;
        }
        return 0;
    }

    // A = 50 ms, M = 100 ms, B = 150 ms
    int processing_time_ms(char type) const {
        switch (type) {
        case 'A': return 50;
        case 'M': return 100;
        case 'B': return 150;
        }
        return 100;
    }

    // Genera tipo de tarea según distribución 10% A, 30% M, 60% B
    char random_task_type(std::mt19937& gen,
        std::discrete_distribution<int>& dist) {
        int idx = dist(gen); // 0 -> A, 1 -> M, 2 -> B
        if (idx == 0) return 'A';
        if (idx == 1) return 'M';
        return 'B';
    }

    // Inserción en la cola con control de capacidad
    void enqueue_task(char type) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_not_full_.wait(lk, [&] {
            return stop_production.load() || queue_.size() < MAX_QUEUE;
            });

        if (stop_production) {
            return; // ya no agregamos más tareas
        }

        Task t;
        t.type = type;
        t.id = next_id++;
        t.enqueue_time = steady_clock::now();
        queue_.push_back(t);

        cv_not_empty_.notify_one();
    }

    // Selección de tarea con aging:
    // prioridad efectiva = base + (tiempo_espera_ms / aging_interval_ms)
    size_t select_task_index_unlocked() {
        if (queue_.empty()) return 0;

        auto now = steady_clock::now();
        const double aging_interval_ms = 200.0; // parámetro de envejecimiento

        double bestScore = -1e9;
        size_t bestIdx = 0;

        for (size_t i = 0; i < queue_.size(); ++i) {
            const Task& t = queue_[i];
            int base = base_priority(t.type);
            auto wait_ms = duration_cast<milliseconds>(now - t.enqueue_time).count();
            double score = base + (double)wait_ms / aging_interval_ms;

            if (score > bestScore) {
                bestScore = score;
                bestIdx = i;
            }
            else if (score == bestScore) {
                // En empate, preferimos la más antigua
                if (t.enqueue_time < queue_[bestIdx].enqueue_time) {
                    bestIdx = i;
                }
            }
        }
        return bestIdx;
    }

    // ---- Lógica de hilos ----

    // Productor: productor 0 genera la secuencia fija de 30 tareas,
    // luego todos generan con distribución probabilística.
    void producer(int producerId) {
        // Random engine por hilo
        std::random_device rd;
        std::mt19937 gen(rd() + producerId * 1000);
        std::discrete_distribution<int> dist({ 10, 30, 60 }); // A, M, B

        // El productor 0 genera la secuencia fija
        if (producerId == 0) {
            for (char type : initial_sequence) {
                if (stop_production) break;
                enqueue_task(type);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            initial_done = true;
        }
        else {
            // Los demás esperan hasta que la secuencia fija haya terminado
            while (!initial_done && !stop_production) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Después de la secuencia fija, todos producen con la distribución dada
        while (!stop_production) {
            char t = random_task_type(gen, dist);
            enqueue_task(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void consumer(int consumerId) {
        (void)consumerId; // no usado, pero lo dejamos por claridad

        while (true) {
            Task task;

            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_not_empty_.wait(lk, [&] {
                    return stop_consumers.load() || !queue_.empty();
                    });

                if (stop_consumers) {
                    return;  // en cuanto nos digan que paremos, salimos aunque haya tareas en cola
                }

                if (queue_.empty()) {
                    continue;
                }

                size_t idx = select_task_index_unlocked();
                task = queue_[idx];
                queue_.erase(queue_.begin() + idx);

                cv_not_full_.notify_one();
            }

            // Contabilizar
            if (task.type == 'A')      ++processedA;
            else if (task.type == 'M') ++processedM;
            else                       ++processedB;

            // Simular tiempo de procesamiento
            std::this_thread::sleep_for(std::chrono::milliseconds(processing_time_ms(task.type)));
        }
    }

    // Hilo de monitor: imprime tabla cada 2 segundos
    void monitor() {
        std::cout << "\n=====================================\n";
        std::cout << "VERSION SIN STARVATION (con aging)\n";
        std::cout << "Tiempo(s)\tA_proc\tM_proc\tB_proc\tB_espera\tEstado_cola\n";
        std::cout << "-------------------------------------\n";

        for (int elapsed = 2; elapsed <= RUN_SECONDS; elapsed += 2) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            int a = processedA.load();
            int m = processedM.load();
            int b = processedB.load();
            int pendingB = 0;
            std::string state;

            {
                std::lock_guard<std::mutex> lk(mtx_);
                pendingB = std::count_if(queue_.begin(), queue_.end(),
                    [](const Task& t) { return t.type == 'B'; });

                std::ostringstream oss;
                for (const auto& t : queue_) {
                    oss << t.type << ' ';
                }
                state = oss.str();
            }

            std::cout << std::setw(8) << elapsed << "\t"
                << std::setw(6) << a << "\t"
                << std::setw(6) << m << "\t"
                << std::setw(6) << b << "\t"
                << std::setw(8) << pendingB << "\t"
                << state << "\n";
        }

        std::cout << "=====================================\n\n";
    }
};

int main() {
    Simulation sim;
    sim.run();
    return 0;
}
