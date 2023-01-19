## VTube Studio 联动

### 简介

`VTube Studio 联动`可以帮助使用`VTube Studio`的虚拟主播进行联动，并且：

1. 无需分享模型，无需顾虑如`PrprLive`基于模型分享联动带来的模型版权及分享的隐患
2. 支持原生透明度，而非基于类似去除绿幕的伪透明度
3. 充分使用了各种硬件加速能力，系统资源占用较低
4. 考虑多种网络环境，自动切换P2P及中转两种传输模式
5. 无需自行部署中转服务器，内置基于腾讯云的中转服务器一键创建部署（付费）

### 代码

[前往介绍页](https://www.wolai.com/reito/dGzCn2JJCB8tnZwWd6wcRN)

### 编译

项目界面基于`Qt`，但使用`vcpkg`及`CMake`进行管理

1. 安装[vcpkg](https://github.com/microsoft/vcpkg)

2. 使用你喜欢的IDE打开`CMakeLists.txt`

3. 选择合适的CMake编译路径及安装路径

4. 设置环境变量，如果IDE支持也可以在IDE对CMake的配置页设置，如`CLion`

| 名称                     | 值来源                  |
|------------------------|----------------------|
| BACKTRACE_SUBMIT_TOKEN | https://backtrace.io |

![image](https://user-images.githubusercontent.com/29846655/212706928-4a4a8271-103a-4adf-a580-d8045152d7dd.png)

5. 编译

### 许可证

[LICENSE](LICENSE)
