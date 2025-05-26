#include <iostream>
#include <thread>
#include <queue>
#include <random>
#include <semaphore>
#include <chrono>
#include <vector>
#include <mutex>
#include <atomic>

// Структура для задачи квантового симулятора
struct QuantumTask {
    int id;
    int priority;       // Чем меньше число - тем выше приоритет (1 - наивысший)
    bool is_critical;   // Флаг критически важной задачи
    int duration;       // Время выполнения в миллисекундах
    int required_qubits;// Требуемое количество кубитов
    bool is_split = false; // Была ли задача разделена
};

// Оператор сравнения для очереди с приоритетами
struct ComparePriority {
    bool operator()(const QuantumTask& t1, const QuantumTask& t2) {
        // Сначала сравниваем по приоритету (меньшее число - выше приоритет)
        if (t1.priority != t2.priority) {
            return t1.priority > t2.priority;
        }
        // Затем критически важные задачи имеют преимущество
        if (t1.is_critical != t2.is_critical) {
            return !t1.is_critical;
        }
        // Затем по времени выполнения (более короткие задачи вперед)
        return t1.duration > t2.duration;
    }
};

std::priority_queue<QuantumTask, std::vector<QuantumTask>, ComparePriority> task_queue;
std::counting_semaphore<4> quantum_processors(4); // 4 квантовых процессора
std::mutex queue_mutex; // Мьютекс для синхронизации доступа к очереди
std::mutex output_mutex; // Мьютекс для вывода в консоль
std::atomic<int> failed_processor(-1); // Идентификатор вышедшего из строя процессора (-1 - все работают)

// Функция для обработки задачи на квантовом процессоре
void process_quantum_task(QuantumTask task, int processor_id) {
    // Проверяем, не вышел ли процессор из строя
    if (processor_id == failed_processor.load()) {
        std::lock_guard<std::mutex> out_lock(output_mutex);
        std::cout << "Task " << task.id << " failed on processor " << processor_id 
                  << " (processor broken)" << std::endl;
        return;
    }

    // Захватываем процессор
    quantum_processors.acquire();
    
    {
        std::lock_guard<std::mutex> out_lock(output_mutex);
        std::cout << "Processor " << processor_id << ": Task " << task.id 
                  << " (priority " << task.priority 
                  << (task.is_critical ? ", CRITICAL" : "") 
                  << ") started. Duration: " << task.duration << "ms" 
                  << (task.is_split ? " (split task)" : "") << std::endl;
    }

    // Имитация выполнения задачи
    std::this_thread::sleep_for(std::chrono::milliseconds(task.duration));

    // Освобождаем процессор
    quantum_processors.release();
    
    {
        std::lock_guard<std::mutex> out_lock(output_mutex);
        std::cout << "Processor " << processor_id << ": Task " << task.id << " completed." << std::endl;
    }
}

// Функция для разделения задачи на более мелкие
QuantumTask split_task(const QuantumTask& original_task) {
    QuantumTask new_task = original_task;
    new_task.id = original_task.id * 100 + rand() % 100; // Новый ID для подзадачи
    new_task.duration = original_task.duration / 2;
    new_task.required_qubits = original_task.required_qubits / 2;
    new_task.is_split = true;
    return new_task;
}

// Функция для добавления задач в очередь
void add_quantum_task(int id, int priority, bool is_critical, int duration, int qubits) {
    QuantumTask task = {id, priority, is_critical, duration, qubits};
    
    std::lock_guard<std::mutex> queue_lock(queue_mutex);
    task_queue.push(task);
    
    std::lock_guard<std::mutex> out_lock(output_mutex);
    std::cout << "Task " << id << " added to queue. Priority: " << priority 
              << (is_critical ? " (CRITICAL)" : "") 
              << ", Duration: " << duration << "ms, Qubits: " << qubits << std::endl;
}

// Функция обработки задач из очереди
void process_quantum_tasks(int processor_id) {
    while (true) {
        std::unique_lock<std::mutex> queue_lock(queue_mutex);
        if (task_queue.empty()) {
            queue_lock.unlock();
            break;
        }
        
        QuantumTask task = task_queue.top();
        task_queue.pop();
        queue_lock.unlock();

        // Проверяем, не нужно ли разделить задачу (если процессор перегружен)
        if (task.required_qubits > 5 && !task.is_split) { // Условная проверка на перегрузку
            std::lock_guard<std::mutex> out_lock(output_mutex);
            std::cout << "Processor " << processor_id << ": Task " << task.id 
                      << " is too large, splitting..." << std::endl;
            
            queue_lock.lock();
            QuantumTask sub_task1 = split_task(task);
            QuantumTask sub_task2 = split_task(task);
            task_queue.push(sub_task1);
            task_queue.push(sub_task2);
            queue_lock.unlock();
            
            continue;
        }

        process_quantum_task(task, processor_id);
    }
}

// Функция для имитации сбоя процессора
void simulate_processor_failure(int processor_id) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    failed_processor.store(processor_id);
    
    std::lock_guard<std::mutex> out_lock(output_mutex);
    std::cout << "!!! Processor " << processor_id << " FAILED !!!" << std::endl;
}

int main() {
    // Инициализация генератора случайных чисел
    std::srand(std::time(nullptr));

    // Добавляем задачи в очередь
    // ID, приоритет, критическая, длительность (мс), кубиты
    add_quantum_task(1, 1, true, 2000, 8);   // Критически важная задача с высоким приоритетом
    add_quantum_task(2, 3, false, 3000, 6);  // Обычная задача
    add_quantum_task(3, 2, false, 1500, 4);  // Средний приоритет
    add_quantum_task(4, 1, false, 2500, 10); // Высокий приоритет, но не критическая
    add_quantum_task(5, 4, true, 1000, 3);   // Критическая, но низкий приоритет
    add_quantum_task(6, 2, true, 1800, 7);   // Критическая со средним приоритетом
    add_quantum_task(7, 3, false, 2200, 5);  // Обычная задача
    add_quantum_task(8, 1, true, 500, 2);    // Важная короткая критическая задача
    add_quantum_task(9, 5, false, 4000, 9);  // Долгая задача с низким приоритетом
    add_quantum_task(10, 2, false, 1200, 3); // Средний приоритет

    // Создаем потоки для обработки задач (10 потоков вычислений)
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.push_back(std::thread(process_quantum_tasks, i % 4)); // 4 процессора
    }

    // Имитируем сбой одного из процессоров через 2 секунды
    std::thread failure_thread(simulate_processor_failure, 2);

    // Ожидаем завершения всех потоков
    for (auto& t : threads) {
        t.join();
    }
    failure_thread.join();

    return 0;
}
