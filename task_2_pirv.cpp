#include <iostream>
#include <thread>
#include <queue>
#include <vector>
#include <semaphore>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>

// Структура данных от станции мониторинга
struct MonitoringData {
    int station_id;     // ID станции (1-10)
    int priority;       // Приоритет данных (1 - высший)
    bool is_critical;   // Критически важные данные
    std::string payload;// Полезная нагрузка (данные)
    size_t size;        // Размер данных в байтах
};

// Компаратор для очереди с приоритетами
struct ComparePriority {
    bool operator()(const MonitoringData& d1, const MonitoringData& d2) {
        // Сначала сравниваем по приоритету (меньшее число - выше приоритет)
        if (d1.priority != d2.priority) {
            return d1.priority > d2.priority;
        }
        // Затем критически важные данные имеют преимущество
        if (d1.is_critical != d2.is_critical) {
            return !d1.is_critical;
        }
        // Затем по размеру (меньшие данные вперед)
        return d1.size > d2.size;
    }
};

// Глобальные переменные
std::priority_queue<MonitoringData, std::vector<MonitoringData>, ComparePriority> data_queue;
std::mutex queue_mutex;
std::mutex cout_mutex;
std::counting_semaphore<5> server_capacity(5); // Начальная емкость сервера - 5 обработчиков
std::atomic<size_t> current_load(0);           // Текущая загрузка сервера в %
std::atomic<bool> emergency_mode(false);       // Режим аварии
std::atomic<int> active_handlers(5);           // Количество активных обработчиков

// Функция для обработки данных на сервере
void process_data(const MonitoringData& data) {
    // Захватываем ресурс сервера
    server_capacity.acquire();
    
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "[Сервер] Обработка данных от станции " << data.station_id 
                  << " (приоритет " << data.priority 
                  << (data.is_critical ? ", КРИТИЧЕСКИЕ" : "") 
                  << "), размер: " << data.size << " байт" << std::endl;
    }

    // Имитация обработки данных (время зависит от размера)
    std::this_thread::sleep_for(std::chrono::milliseconds(data.size / 100));
    
    // Освобождаем ресурс
    server_capacity.release();
    
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "[Сервер] Данные от станции " << data.station_id << " обработаны" << std::endl;
    }
}

// Функция мониторинговой станции
void monitoring_station(int station_id) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(100, 1000); // Размер данных 100-1000 байт
    std::uniform_int_distribution<> priority_dist(1, 5);  // Приоритет 1-5
    std::bernoulli_distribution critical_dist(0.2);       // 20% критических данных
    
    while (true) {
        // Генерируем данные
        MonitoringData data;
        data.station_id = station_id;
        data.priority = priority_dist(gen);
        data.is_critical = critical_dist(gen);
        data.size = size_dist(gen);
        data.payload = "Данные мониторинга от станции " + std::to_string(station_id);
        
        // Проверяем перегрузку сервера
        if (current_load > 80 && !data.is_critical && data.priority > 3) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "[Станция " << station_id << "] Данные отброшены (перегрузка сервера)" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        
        // В режиме аварии отбрасываем низкоприоритетные данные
        if (emergency_mode && data.priority > 2 && !data.is_critical) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "[Станция " << station_id << "] Данные отброшены (режим аварии)" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        
        // Добавляем данные в очередь
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            data_queue.push(data);
        }
        
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "[Станция " << station_id << "] Отправлены данные (приоритет " 
                      << data.priority << (data.is_critical ? ", КРИТИЧЕСКИЕ" : "") 
                      << "), размер: " << data.size << " байт" << std::endl;
        }
        
        // Имитация временного интервала между отправками
        std::this_thread::sleep_for(std::chrono::milliseconds(300 + size_dist(gen)));
    }
}

// Функция обработчика данных
void data_handler() {
    while (true) {
        MonitoringData data;
        bool data_available = false;
        
        // Безопасное извлечение данных из очереди
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (!data_queue.empty()) {
                data = data_queue.top();
                data_queue.pop();
                data_available = true;
            }
        }
        
        if (data_available) {
            process_data(data);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// Функция мониторинга загрузки сервера
void load_monitor() {
    while (true) {
        // Рассчитываем текущую загрузку
        size_t load = (5 - server_capacity.try_acquire()) * 20; // Процентная загрузка
        
        // Пытаемся получить больше семфоров для точного расчета
        for (int i = 0; i < 4; i++) {
            if (server_capacity.try_acquire()) {
                server_capacity.release();
                break;
            }
        }
        
        current_load = load;
        
        // Адаптация к нагрузке
        if (load > 80 && active_handlers < 10) {
            // Увеличиваем количество обработчиков при высокой нагрузке
            active_handlers++;
            server_capacity.release();
            
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "[Монитор] Загрузка сервера " << load 
                      << "%. Добавлен обработчик. Всего: " << active_handlers << std::endl;
        } 
        else if (load < 50 && active_handlers > 5) {
            // Уменьшаем количество обработчиков при низкой нагрузке
            active_handlers--;
            server_capacity.acquire();
            
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "[Монитор] Загрузка сервера " << load 
                      << "%. Удален обработчик. Всего: " << active_handlers << std::endl;
        }
        
        // Проверка на аварийную ситуацию (имитация)
        static std::atomic<int> emergency_counter(0);
        if (load > 95) {
            emergency_counter++;
            if (emergency_counter > 3 && !emergency_mode) {
                emergency_mode = true;
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "[Монитор] АВАРИЙНЫЙ РЕЖИМ! Только критические данные!" << std::endl;
            }
        } else {
            emergency_counter = 0;
            if (emergency_mode) {
                emergency_mode = false;
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "[Монитор] Аварийный режим отключен" << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main() {
    // Создаем станции мониторинга
    std::vector<std::thread> stations;
    for (int i = 1; i <= 10; ++i) {
        stations.emplace_back(monitoring_station, i);
    }
    
    // Создаем обработчики данных (начальное количество - 5)
    std::vector<std::thread> handlers;
    for (int i = 0; i < 5; ++i) {
        handlers.emplace_back(data_handler);
    }
    
    // Запускаем монитор загрузки
    std::thread monitor(load_monitor);
    
    // Имитация работы системы (30 секунд)
    std::this_thread::sleep_for(std::chrono::seconds(30));
    
    // Завершение работы (в реальной системе было бы по-другому)
    for (auto& station : stations) {
        station.detach();
    }
    for (auto& handler : handlers) {
        handler.detach();
    }
    monitor.detach();
    
    return 0;
}
