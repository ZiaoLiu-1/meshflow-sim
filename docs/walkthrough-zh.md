# 从八个函数读懂 MeshFlow

可以先从下面八个函数入手。读一段代码，预测一个小输入的结果，再运行验证；每次只改一个条件，会更容易看出规则是怎样起作用的。

1. [`make_workload`](../src/workloads.cpp)：从输入构造指令和初始内存。scan 的输入在每个 PE 内存的前半段，输出在后半段，最后一格存截至该 PE 的总和。它生成 LOAD/ADD/STORE 和邻居 SEND/RECV，运行前没有把正确前缀和塞进去。**问题：把 5 个输入分给 3 个 PE 时，三个 chunk 各有几个数？**
2. [`instruction_error`](../src/model.cpp)：在发起前检查指令实际使用的寄存器、内存下标和邻居编号；负数也必须拒绝。HALT 不使用操作数，不能误判其无关字段。**问题：为什么 SEND 不能把自己当成邻居？**
3. [`checked_add`](../src/model.cpp)：先转成 int64 求和，再检查 int32 范围；如果溢出，返回空的 optional，执行器在写回前 fault。**问题：INT32_MAX 加 1 后，目标寄存器是否会被部分修改？**
4. [`TickEngine::run`](../src/tick_engine.cpp)：每个整数 tick 分别扫描完成和发起。SEND 发起即占容量，完成才进入可见 FIFO；RECV 发起即弹出消息，完成才写寄存器。PC 在完成时前进。**问题：link latency=3 的 SEND 在 tick 1 发起，接收方最早在哪个 tick 能取得这条消息？**
5. [`LaterEvent::operator()`](../src/event_engine.cpp)：优先队列按 tick、phase、稳定的 PE ID 排序。比较器使用“更晚”的关系，让最早事件出现在队首。每 PE 最多一个 pending 操作，因此相同 tick 的完成事件用 PE ID 就能稳定排序。**问题：同 tick 的两个写回为什么不会随 heap 的内部排列改变顺序？**
6. [`EventEngine::run`](../src/event_engine.cpp)：只在完成事件或必要的下一 tick 重试时推进。所有到期写回完成后才发起下一轮；较低 ID 的 sender 错过较高 ID 的 receiver 释放的空位时，必须加一个下一 tick 的 wake。**问题：省掉这个 wake 会把哪个正常等待情形错误地推迟？**
7. [`difference`](../src/model.cpp)：依次比较终止原因、tick、PE 状态、完整内存、链路记录和 trace；checksum 只是方便查看，不能代替这些检查。**问题：最终 sum 一样但 RECV 的 sequence 不同，比较是否应该通过？**
8. [`oracle_error`](../src/workloads.cpp)：用原始输入做简单的串行数学检查。scan 核对每个前缀，reduction 核对每个 PE 的累计总和，relay 核对每一跳保存的数据。**问题：为什么两个执行器互相一致仍然需要这层 oracle？**

首次动手可运行 README 的 size=65 scan，再只把 `--capacity 1` 改成 `--capacity 2`，预测结果与 tick 是否会变化并检查输出。随后构造一个两 PE、两个消息的程序，画出每 tick 的 FIFO 和 in-flight 计数，再对照 goldens。
