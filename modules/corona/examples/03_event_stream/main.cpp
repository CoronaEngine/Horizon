// EventStream Example - 展示响应式数据流
// 演示如何使用EventStream进行流式数据处理、转换和过滤

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_stream.h"

using namespace Corona::Kernel;

// ========================================
// 定义数据结构
// ========================================

struct SensorData {
    int sensor_id;
    double value;
    std::string unit;
};

struct ProcessedData {
    int sensor_id;
    double processed_value;
    bool is_anomaly;
};

// ========================================
// 主程序
// ========================================

int main() {
    std::cout << "=== Corona Framework - EventStream Example ===" << std::endl;
    std::cout << std::endl;

    // 初始化内核
    auto& kernel = KernelContext::instance();
    if (!kernel.initialize()) {
        std::cerr << "Failed to initialize kernel!" << std::endl;
        return 1;
    }

    // ========================================
    // 示例 1: 基本的流订阅
    // ========================================
    std::cout << "[Example 1] Basic Stream Subscription" << std::endl;

    auto stream1 = create_event_stream<int>();

    // 订阅流 - 在单独的线程中处理
    auto sub1 = stream1->subscribe();
    std::thread consumer1([&sub1]() {
        while (auto value = sub1.wait_for(std::chrono::milliseconds(100))) {
            std::cout << "  Received: " << *value << std::endl;
        }
    });

    // 发送数据
    stream1->publish(10);
    stream1->publish(20);
    stream1->publish(30);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sub1.close();
    consumer1.join();
    std::cout << std::endl;

    // ========================================
    // 示例 2: 多个订阅者
    // ========================================
    std::cout << "[Example 2] Multiple Subscribers" << std::endl;

    auto stream2 = create_event_stream<int>();

    // 两个订阅者处理同一个流
    auto sub2a = stream2->subscribe();
    auto sub2b = stream2->subscribe();

    std::thread consumer2a([&sub2a]() {
        while (auto value = sub2a.wait_for(std::chrono::milliseconds(100))) {
            std::cout << "  Consumer A received: " << *value << std::endl;
        }
    });

    std::thread consumer2b([&sub2b]() {
        while (auto value = sub2b.wait_for(std::chrono::milliseconds(100))) {
            std::cout << "  Consumer B received: " << *value << std::endl;
        }
    });

    stream2->publish(5);
    stream2->publish(10);
    stream2->publish(15);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sub2a.close();
    sub2b.close();
    consumer2a.join();
    consumer2b.join();
    std::cout << std::endl;

    // ========================================
    // 示例 3: 手动过滤处理
    // ========================================
    std::cout << "[Example 3] Manual Filtering" << std::endl;

    auto stream3 = create_event_stream<int>();

    // 订阅者手动过滤偶数
    auto sub3 = stream3->subscribe();
    std::thread consumer3([&sub3]() {
        while (auto value = sub3.wait_for(std::chrono::milliseconds(100))) {
            if (*value % 2 == 0) {
                std::cout << "  Even number: " << *value << std::endl;
            }
        }
    });

    for (int i = 1; i <= 10; ++i) {
        stream3->publish(i);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sub3.close();
    consumer3.join();
    std::cout << std::endl;

    // ========================================
    // 示例 4: 过滤和转换
    // ========================================
    std::cout << "[Example 4] Filter and Transform" << std::endl;

    auto stream4 = create_event_stream<int>();

    // 订阅者：过滤正数 -> 计算平方
    auto sub4 = stream4->subscribe();
    std::thread consumer4([&sub4]() {
        while (auto value = sub4.wait_for(std::chrono::milliseconds(100))) {
            if (*value > 0) {  // 过滤正数
                int squared = (*value) * (*value);
                std::cout << "  Squared positive: " << squared << std::endl;
            }
        }
    });

    stream4->publish(-5);  // 被过滤掉
    stream4->publish(3);   // 输出 9
    stream4->publish(0);   // 被过滤掉
    stream4->publish(4);   // 输出 16

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sub4.close();
    consumer4.join();
    std::cout << std::endl;

    // ========================================
    // 示例 5: 实际应用 - 传感器数据处理
    // ========================================
    std::cout << "[Example 5] Real-world Example - Sensor Data Processing" << std::endl;

    auto sensor_stream = create_event_stream<SensorData>();

    // 数据处理管道
    auto sensor_sub = sensor_stream->subscribe();
    std::thread sensor_processor([&sensor_sub]() {
        while (auto data = sensor_sub.wait_for(std::chrono::milliseconds(100))) {
            // 过滤：只处理有效范围内的数据
            if (data->value < 0 || data->value > 100) {
                continue;
            }

            // 转换：归一化并检测异常
            double normalized = data->value / 100.0;
            bool anomaly = (normalized > 0.9 || normalized < 0.1);

            ProcessedData processed{
                data->sensor_id,
                normalized,
                anomaly};

            // 处理结果
            if (processed.is_anomaly) {
                CFW_LOG_WARNING("Sensor {}: {} [WARNING] ANOMALY DETECTED!",
                                processed.sensor_id, processed.processed_value);
            } else {
                CFW_LOG_INFO("Sensor {}: {}",
                             processed.sensor_id, processed.processed_value);
            }
        }
    });

    // 模拟传感器数据
    sensor_stream->publish(SensorData{1, 45.5, "C"});
    sensor_stream->publish(SensorData{2, 95.0, "C"});  // 异常值
    sensor_stream->publish(SensorData{1, 50.2, "C"});
    sensor_stream->publish(SensorData{3, 5.0, "C"});    // 异常值
    sensor_stream->publish(SensorData{2, 110.0, "C"});  // 超出范围,被过滤

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sensor_sub.close();
    sensor_processor.join();
    std::cout << std::endl;

    // ========================================
    // 示例 6: try_pop 非阻塞读取
    // ========================================
    std::cout << "[Example 6] Non-blocking Read with try_pop" << std::endl;

    auto stream6 = create_event_stream<int>();
    auto sub6 = stream6->subscribe();

    // 先发布一些数据
    stream6->publish(100);
    stream6->publish(200);

    // 非阻塞读取
    if (auto value = sub6.try_pop()) {
        std::cout << "  Got value: " << *value << std::endl;
    }

    if (auto value = sub6.try_pop()) {
        std::cout << "  Got value: " << *value << std::endl;
    }

    // 队列为空时返回 nullopt
    if (auto value = sub6.try_pop()) {
        std::cout << "  Got value: " << *value << std::endl;
    } else {
        std::cout << "  Queue is empty" << std::endl;
    }

    sub6.close();
    std::cout << std::endl;

    // ========================================
    // 示例 7: wait 阻塞读取
    // ========================================
    std::cout << "[Example 7] Blocking Read with wait" << std::endl;

    auto stream7 = create_event_stream<int>();
    auto sub7 = stream7->subscribe();

    std::thread producer7([&stream7]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stream7->publish(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stream7->publish(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stream7->publish(3);
    });

    std::thread consumer7([&sub7]() {
        for (int i = 0; i < 3; ++i) {
            if (auto value = sub7.wait()) {  // 无限等待
                std::cout << "  Processing: " << *value << std::endl;
            }
        }
        std::cout << "  Stream processing completed!" << std::endl;
    });

    producer7.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sub7.close();
    consumer7.join();
    std::cout << std::endl;

    // ========================================
    // 示例 8: 背压处理
    // ========================================
    std::cout << "[Example 8] Backpressure Handling" << std::endl;

    EventStreamOptions options;
    options.max_queue_size = 3;  // 小缓冲区
    options.policy = BackpressurePolicy::DropOldest;

    auto stream8 = create_event_stream<int>();
    auto sub8 = stream8->subscribe(options);

    int processed_count = 0;
    std::thread consumer8([&sub8, &processed_count]() {
        while (auto value = sub8.wait_for(std::chrono::milliseconds(200))) {
            processed_count++;
            std::cout << "  Processing #" << processed_count << ": " << *value << std::endl;
            // 模拟慢速消费者
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    // 快速生产数据
    for (int i = 1; i <= 5; ++i) {
        stream8->publish(i);
        std::cout << "  Published: " << i << std::endl;
    }

    // 等待处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    sub8.close();
    consumer8.join();
    std::cout << std::endl;

    // ========================================
    // 性能测试
    // ========================================
    std::cout << "[Performance Test] Processing 10,000 events..." << std::endl;

    auto perf_stream = create_event_stream<int>();
    auto perf_sub = perf_stream->subscribe();

    std::atomic<int> count{0};
    std::thread perf_consumer([&perf_sub, &count]() {
        while (auto value = perf_sub.wait_for(std::chrono::milliseconds(100))) {
            count++;
        }
    });

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        perf_stream->publish(i);
    }
    auto end = std::chrono::high_resolution_clock::now();

    // 等待消费完成
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    perf_sub.close();
    perf_consumer.join();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "  Published 10,000 events in "
              << duration.count() << " ms" << std::endl;
    std::cout << "  Processed " << count << " events" << std::endl;
    std::cout << "  Throughput: " << (10000.0 * 1000.0 / duration.count())
              << " events/second" << std::endl;

    // 清理
    kernel.shutdown();

    std::cout << std::endl;
    std::cout << "=== Example completed successfully ===" << std::endl;
    return 0;
}
