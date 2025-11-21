# Concurrencia en C++ — Starvation, Race Condition y Deadlock

Este repositorio contiene la implementación de tres escenarios clásicos de concurrencia y sincronización usando **C++11** y la biblioteca estándar de threads (`std::thread`, `std::mutex`, `std::condition_variable`):

- **Starvation**  
- **Race Condition**  
- **Deadlock**

Los tres experimentos incluyen:
- Una **versión con problema** (sin mecanismos de sincronización correctos)  
- Una **versión solucionada**  
- Evidencia experimental  
- Resultados y análisis  


## Estructura del repositorio
```
Sistemas-Operativos-Concurrencia/
│
├── starvation/
│   ├── starvation_con_problema.cpp
│   └── starvation_solucion.cpp
│
├── race_condition/
│   ├── race_condition_con_problema.cpp
│   └── race_condition_solucion.cpp
│
└── deadlocks/
    ├── deadlock_con_problema.cpp
    └── deadlock_solucion.cpp
```
# Escenario 1 — Starvation

### Objetivo  
Simular un sistema de procesamiento de tareas con prioridades (A, M, B) donde las tareas de baja prioridad (**B**) pueden sufrir **hambre (starvation)**.

### Características del sistema  
- Cola de tareas con capacidad: **20**  
- 3 tipos de tareas:  
  - Alta prioridad (**A**, 50 ms)  
  - Media prioridad (**M**, 100 ms)  
  - Baja prioridad (**B**, 150 ms)  
- 5 productores (con distribución 10% A, 30% M, 60% B)  
- 3 consumidores  
- Monitoreo cada 2 s

### Versión con Starvation  
- Los consumidores siempre procesan: **A > M > B**  
- **Las tareas B quedan sin procesarse** al finalizar los 10s  
- Se observa acumulación constante de B en cola  
- Tiempo de espera extremo para B

### Versión sin Starvation (Aging)  
- Cada tarea incrementa su prioridad efectiva según tiempo de espera  
- No hay starvation  
- La cola se vacía completamente  
- Todas las tareas son procesadas

# Escenario 2 — Race Condition (Gestor de Inventario)

### Objetivo  
Simular actualizaciones concurrentes sobre un inventario para mostrar:

- **Race conditions** cuando no hay protección  
- Corrección al usar **mutex**

### Características  
- 10 productos  
- Stock inicial: 100 unidades  
- 20 threads  
- Operaciones: `vender()` y `reabastecer()`

### Versión con Race Condition  
- No hay sincronización  
- Lecturas/escrituras no atómicas  
- Resultados inconsistentes  
- Ejecuciones repetidas generan diferentes valores finales

### Versión sin Race Condition (Mutex por producto)  
- Se usa `std::mutex` para proteger `stock[id]`  
- Las 10 ejecuciones consistentemente muestran los valores correctos:
  - Producto 0 → **120**
  - Producto 5 → **110**
  
# Escenario 3 — Deadlock (Banco con Transferencias)

### Objetivo  
Simular transferencias concurrentes entre cuentas en un sistema bancario:

- Mostrar cómo un mal manejo de locks provoca **deadlocks**
- Aplicar una solución basada en **orden global de recursos**

### Características  
- 5 cuentas bancarias (ID 0–4)  
- Saldo inicial: $1000 × (ID + 1)  
- 10 threads con 3 transferencias cada uno  
- Total: 30 transferencias  

### Versión con Deadlock  
- Cada transferencia bloquea:
  1. `from`
  2. `to`
- Sin orden → ciclos de espera  
- Deadlock reproducible:
  - T1 bloquea 0 y espera 1  
  - T2 bloquea 1 y espera 0  
  - Otros threads quedan esperándolos  
- 0/30 transferencias completadas

### Versión sin Deadlock (Orden global)  
- Siempre se adquieren locks en el orden:  
  `min(from, to)` → `max(from, to)`
- Eliminación total de espera circular  
- 30/30 transferencias exitosas  
- Saldos finales coherentes  
- No existe pérdida de dinero en el sistema

# Cómo compilar y ejecutar

### Requisitos  
- C++11 o superior  
- g++, clang++ o MSVC  
- Sistema operativo: Windows, Linux o macOS  

### Compilación (g++)  

```bash
g++ -std=c++11 -pthread starvation/starvation_con_problema.cpp -o starvation_con
g++ -std=c++11 -pthread starvation/starvation_solucion.cpp -o starvation_sol

g++ -std=c++11 -pthread race_condition/race_condition_con_problema.cpp -o rc_con
g++ -std=c++11 -pthread race_condition/race_condition_solucion.cpp -o rc_sol

g++ -std=c++11 -pthread deadlocks/deadlock_con_problema.cpp -o dl_con
g++ -std=c++11 -pthread deadlocks/deadlock_solucion.cpp -o dl_sol
```

###Ejecución

```bash
./starvation_con
./starvation_sol

./rc_con
./rc_sol

./dl_con
./dl_sol
```

### Entorno de desarrollo 
- Lenguaje:	C++ (C++11)
- Compilador:	MSVC (Visual Studio)
- SO:	Windows 11
- Librería de threads:	std::thread, std::mutex, std::condition_variable
- Hardware:	Intel Core i5-13420H (13th Gen), 8 Cores / 12 Threads @ 2.10 GHz, 16 GB RAM
