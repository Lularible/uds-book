# UDS技术书 — 从望闻问切到UDS协议实现

一本从诊断元问题出发，直通ISO 14229协议规范与AUTOSAR DCM源码、再到亲手实现UDS栈的开源技术书。

## 运行效果
<img width="1475" height="920" alt="uds_demo" src="https://github.com/user-attachments/assets/5518ae1a-9abf-471c-bf2a-16bdd60f3a26" />


## 这本书讲了什么

全书 48 节，分五章：

- **第一章（5 节）**：从"你会看病吗"这个元问题出发，讲诊断的本质与UDS在汽车电子中的位置——中医四诊、OBD-II历史、KWP2000与UDS的继承关系
- **第二章（23 节）**：逐服务拆解UDS协议——全部26个SID、会话/安全/读写DTC/DID/上传下载/文件传输等，每节一个服务，附字节流实例和NRC分析
- **第三章（4 节）**：传输层——ISO 15765-2 CAN多帧传输的流控状态机、DoIP协议封装与路由激活、CAN与DoIP的性能对比
- **第四章（10 节）**：走进Arctic Core AUTOSAR DCM源码——DSL会话层、DSD调度层、DSP执行层、CanTp传输层、Lcfg配置系统，共约13000行C代码拆解
- **第五章（6 节）**：从零实现一个教学级UDS栈（uds-lite，约1000行C）——报文编解码、ECU服务器、诊断仪客户端、交互式shell、全线测试

**不需要ISO 14229标准文本在手边。每一节写完一个SID，你就"实现"了它一次。**

## 快速开始

在线阅读：直接浏览 `chapters/` 目录下的 Markdown 文件，按文件名前缀数字顺序阅读。

运行 uds-lite 教学代码：

```bash
git clone https://github.com/Lularible/uds-book.git
cd uds-book/uds-lite
make

# 终端 A：ECU 模拟器
./build/x86-64/uds_server

# 终端 B：自动化诊断工作流
./build/x86-64/uds_client

# 或者：交互式诊断终端
./build/x86-64/uds_shell
```

## 许可证

书籍内容：[CC BY-NC-ND 4.0](LICENSE) · uds-lite 源码：MIT

## 姊妹篇

本书是"汽车电子七部曲"系列中的一部。另外五部已发布：

- **[从沙子到车辙——一个工程师的理解](https://github.com/Lularible/from-sand-to-ruts)** — 从图灵机到 CAN 总线，从半导体物理到 AUTOSAR，一部为汽车电子工程师写的全景入门
- **[PTP 技术书——从思想实验到协议实现](https://github.com/Lularible/ptp-book)** — 从时间同步的思想实验到 PTP 协议源码与实现
- **[HSM 技术书——从思想实验到安全基石](https://github.com/Lularible/hsm-book)** — 从岩画密码学到硬件安全模块，完整覆盖车载 HSM 的技术链路
- **[存储 技术书——在不可靠的硬件上构建可靠的数据家园](https://github.com/Lularible/storage-book)** — 一本关于存储技术演进与文件系统实现的深度技术书籍
- **[功能安全——ISO 26262分析与代码实现](https://github.com/Lularible/safety-book-iso26262)** — 以免疫系统为叙事线索的功能安全技术书。兼顾ISO 26262标准分析、源码拆解与动手实现

"汽车电子七部曲"是一个持续更新的系列——还有软件工程在打磨中。
如果觉得本书对你有用，不妨给个 ⭐ 关注进度。
