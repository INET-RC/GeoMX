  - [Synchronization Algorithms](#synchronization-algorithms)
    - [Fully-Synchronous Algorithm](#fully-synchronous-algorithm)
    - [Mixed-Synchronous Algorithm](#mixed-synchronous-algorithm)
    - [Use DCASGD Optimizer in MixedSync](#use-dcasgd-optimizer-in-mixedsync)
    - [Hierarchical Frequency Aggregation](#hierarchical-frequency-aggregation)
  - [Communication Optimization Techniques](#communication-optimization-techniques)
    - [Bidirectional Gradient Sparsification](#bidirectional-gradient-sparsification)
    - [Low-Precision Quantization](#low-precision-quantization)
    - [Mixed-Precision Quantization](#mixed-precision-quantization)
    - [Differential Gradient Transmission](#differential-gradient-transmission)
    - [TSEngine](#tsengine)
    - [Priority-based Parameter Propagation](#priority-based-parameter-propagation)

# Configuration Guide
This guidance describes the environmental variables and hyperparameters needed to launch each optimization technique in our GeoMX system.

## Synchronization Algorithms
GeoMX currently supports two fundamental synchronization algorithms, i.e., the fully-synchronous algorithm, the mixed-synchronous algorithm, and an advanced algorithm, i.e., hierarchical frequency aggregation.

### Fully-Synchronous Algorithm
Fully-Synchronous Algorithm (FSA) is the default strategy for model synchronization. In this synchronous algorithm, training nodes synchronize their model data (can be parameters or gradients) each round, and both parameter server systems within and between data centers run in a synchronous parallel mode. All training nodes are synchronized to ensure a consistent model. However, this comes at the expense of training speed, as it requires waiting for all computations and communications to complete at every iteration.

To use FSA, all that's required is to set `dist_sync` as a hyperparameter during the initialization of `kvstore`. For example:

```python
import mxnet as mx

# Initialize distributed kvstore in synchronous mode.
kvstore_dist = mx.kv.create("dist_sync")

# Master worker sets the optimizer of the global parameter server to Adam.
if kvstore_dist.is_master_worker:
    kvstore_dist.set_optimizer(mx.optimizer.Adam(learning_rate=lr))

for epoch in range(num_epochs):
    for _, batch in enumerate(train_iter):
        # Perform forward and backward propagation to calculate gradients.
        ...
        # Synchronize gradients to obtain updated parameters.
        for idx, param in enumerate(net_params):
            if param.grad_req == "null": continue
            kvstore_dist.push(idx, param.grad(), priority=-idx)
            kvstore_dist.pull(idx, param.data(), priority=-idx)
```

The demo code can be found in [`examples/cnn.py`](https://github.com/INET-RC/GeoMX/blob/main/examples/cnn.py). You can run this demo by simply `bash scripts/xpu/run_vanilla_hips.sh`, where `xpu` should be `cpu` or `gpu`.

### Mixed-Synchronous Algorithm
Mixed-Synchronous Algorithm (MixedSync) is an asynchronous version of FSA, where the difference is that the parameter server system between data centers runs in an asynchronous parallel mode. This setup is particularly suitable for scenarios where intra-data center training nodes display high homogeneity, yet there is significant resource heterogeneity between different data centers. This asynchronous method resolves the problem of straggling data centers, thereby accelerating distributed training across WANs.

To use MixedSync, all that's required is to set `dist_async` instead of `dist_sync` when initializing `kvstore`. The rest of the setup remains the same:

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_async")
```

Alternatively, you can enable MixedSync by using the `--mixed-sync` option in our provided python script:

```commandline
python examples/cnn.py --mixed-sync
```

You can also run this demo by executing `bash scripts/xpu/run_mixed_sync.sh`, where `xpu` should be `cpu` or `gpu`.

### Use DCASGD Optimizer in MixedSync
To alleviate the issue of stale gradients in asynchronous parallel operations, the global parameter server can be configured to use the [DCASGD](http://proceedings.mlr.press/v70/zheng17b/zheng17b.pdf) optimizer. This adjustment aids in improving training convergence while preserving model accuracy.

The way to enable DCASGD in MixedSync is the same as in MXNET: simply replace the `Adam` optimizer with the `DCASGD` optimizer:

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_async")
if kvstore_dist.is_master_worker:
    kvstore_dist.set_optimizer(mx.optimizer.DCASGD(learning_rate=lr))
```

We can use the following command to enable the DCASGD optimizer in MixedSync:

```commandline
python examples/cnn.py --mixed-sync --dcasgd
```

Just modify `scripts/xpu/run_mixed_sync.sh` and try it!

### Hierarchical Frequency Aggregation
Inspired by [this paper](https://ieeexplore.ieee.org/abstract/document/9148862), our Hierarchical Frequency Aggregation (HFA) algorithm first performs $K_1$ steps of local updates at the training nodes, followed by $K_2$ steps of synchronizations at the local parameter server. Finally, a global synchronization is performed at the global parameter server. This approach effectively reduces the frequency of model synchronization across data centers, thereby boosting distributed training.

To enable HFA, we initialize `kvstore` in `dist_sync` mode and make a simple modification to the training loop:

```python
import mxnet as mx

# Initialize distributed kvstore in synchronous mode.
kvstore_dist = mx.kv.create("dist_sync")

# Obtain K1 from environmental variables.
period_k1 = int(os.getenv('MXNET_KVSTORE_HFA_K1'))

# Obtain the number of training nodes in each data center.
num_local_workers = kvstore_dist.num_workers

# Define local trainer to use Adam optimizer.
optimizer = mx.optimizer.Adam(learning_rate=lr)
trainer = Trainer(net.collect_params(), optimizer=optimizer)

global_iters = 1
for epoch in range(num_epochs):
    for _, batch in enumerate(train_iter):
        # Perform forward and backward propagation to calculate gradients.
        ...
        # Update local model parameters.
        trainer.step(num_samples)
        # Synchronize model parameters every K1 round.
        if global_iters % period_k1 == 0:
            for idx, param in enumerate(net_params):
                kvstore_dist.push(idx, param.data() / num_local_workers, priority=-idx)
                kvstore_dist.pull(idx, param.data(), priority=-idx)
        # Update the iteration counter
        global_iters += 1
```

Then, let's set three environmental variables:

```shell
MXNET_KVSTORE_USE_HFA = 1  # whether HFA is enabled
MXNET_KVSTORE_HFA_K1 = 20  # number of loops before a local synchronization
MXNET_KVSTORE_HFA_K2 = 10  # number of loops before a global synchronization
```

The demo code can be found in [`examples/cnn_hfa.py`](https://github.com/INET-RC/GeoMX/blob/main/examples/cnn_hfa.py). You can run this demo by simply `bash scripts/xpu/run_hfa_sync.sh`, where `xpu` should be `cpu` or `gpu`.

## Communication Optimization Techniques
Given the often limited and varied network conditions in WANs, distributed training across data centers can potentially create communication bottlenecks. To mitigate these issues, GeoMX employs a variety of optimization techniques. These include gradient sparsification, mixed-precision quantization, advanced transmission protocols, synchronization algorithms, flow scheduling, and priority scheduling, among others (e.g., overlay scheduling, currently in development). These techniques comprehensively tackle communication issues, further enhancing the efficiency and robustness of distributed machine learning training in GeoMX.

> NOTE: Please try these optimization techniques in the default fully-synchronous mode.

### Bidirectional Gradient Sparsification
Traditional approaches such as [Deep Gradient Compression](https://arxiv.org/pdf/1712.01887.pdf) sparsify the pushed gradient tensors. For further compression, we also sparsify the pulled (aggregated) gradient tensors rather than pulling full parameters. This technique is enabled between the global parameter server and the intra-domain parameter servers of different data centers. (Refer to [this paper](https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf) for more details.)

To enable bidirectional gradient sparsification, define it in `kvstore_dist.set_gradient_compression` and set the compression ratio:

```python
import mxnet as mx

# Initialize distributed kvstore in synchronous mode.
kvstore_dist = mx.kv.create("dist_sync")

# Obtain the total number of training nodes.
num_all_workers = kvstore_dist.num_all_workers

# Master worker enables bidirectional gradient sparsification on the global parameter server.
if kvstore_dist.is_master_worker:
    kvstore_dist.set_gradient_compression({"type": "bsc", "threshold": 0.01})

# Define local trainer to use Adam optimizer.
optimizer = mx.optimizer.Adam(learning_rate=lr)
trainer = Trainer(net.collect_params(), optimizer=optimizer)

for epoch in range(num_epochs):
    for _, batch in enumerate(train_iter):
        # Perform forward and backward propagation to calculate gradients.
        ...
        # Synchronize gradients for gradient aggregation.
        for idx, param in enumerate(net_params):
            if param.grad_req == "null": continue
            kvstore_dist.push(idx, param.grad(), priority=-idx)
            kvstore_dist.pull(idx, param.grad(), priority=-idx)
        # Use aggregated gradients to update local model parameters.
        trainer.step(num_all_workers * batch_size)
        # Put gradients to zero manually.
        for param in net_params:
            param.zero_grad()
```

Note that gradient tensors are classified into large and tiny tensors based on their size, and only the large tensors will be sparsified for transmission. The threshold for classifying large and tiny tensors can be set through the environmental variable `MXNET_KVSTORE_SIZE_LOWER_BOUND`. For example:

```shell
MXNET_KVSTORE_SIZE_LOWER_BOUND = 200000
```

The demo code can be found in [`examples/cnn_bsc.py`](https://github.com/INET-RC/GeoMX/blob/main/examples/cnn_bsc.py). You can run this demo by simply `bash scripts/xpu/run_bisparse_compression.sh`, where `xpu` should be `cpu` or `gpu`.

### Low-Precision Quantization
GeoMX also supports quantifying model data at lower precision for transmission, such as in FP16 format. In this scheme, GeoMX computes the model using FP32, but during transmission, it converts the model data tensor into FP16. Once the pulling data is received, GeoMX reverts it back into FP32 and continues model computing. This effectively halves the data traffic volume over both LANs and WANs.

To quantify model data for transmission in FP16 format, we can simply convert the numerical precision of tensors in our Python code using `astype('float16')`:

```python
import mxnet as mx

# Initialize distributed kvstore in synchronous mode.
kvstore_dist = mx.kv.create("dist_sync")
is_master_worker = kvstore_dist.is_master_worker

# Initialize 16-bit kvstore space on parameter servers to store model parameters or gradients.
for idx, param in enumerate(net_params):
    init_buff = param.data().astype('float16')
    kvstore_dist.init(idx, init_buff)
    if is_master_worker: continue
    kvstore_dist.pull(idx, init_buff)
    param.set_data(init_buff.astype('float32'))

for epoch in range(num_epochs):
    for _, batch in enumerate(train_iter):
        # Perform forward and backward propagation to calculate gradients.
        ...
        # Synchronize gradients for gradient aggregation.
        for idx, param in enumerate(net_params):
            if param.grad_req == "null": continue
            # Push / pull large tensors in 16 bits.
            grad_buff = param.grad().astype('float16')
            kvstore_dist.push(idx, grad_buff, priority=-idx)
            kvstore_dist.pull(idx, grad_buff, priority=-idx)
            # Convert received gradient tensors back to 32 bits.
            param.grad()[:] = grad_buff.astype('float32')
        # Use aggregated gradients to update local model parameters.
        trainer.step(num_all_workers * batch_size)
        # Put gradients to zero manually.
        for param in net_params:
            param.zero_grad()
```

The demo code is provided in [`examples/cnn_fp16.py`](https://github.com/INET-RC/GeoMX/blob/main/examples/cnn_fp16.py), we can run it using `bash scripts/xpu/run_fp16.sh`, where `xpu` should be `cpu` or `gpu`.

### Mixed-Precision Quantization
The technology of Mixed-Precision Quantization (MPQ) leverages both Bi-Sparse and FP16. In this scheme, tiny tensors are quantified into FP16 format for transmission, while large tensors persist in the FP32 format. Moreover, these large sensors will undergo a sparsification process before transmission. This precaution is taken to minimize the loss of crucial information and avoid significant degradation to model performance.

<p align="center">
  Table 1: Summary of the application scope for Bi-Sparse, FP16, and MPQ.
</p>
<table align="center">
  <tr>
    <th rowspan="2"></th>
    <th colspan="2" style="text-align:center; vertical-align:middle">Intra-Data Center</th>
    <th colspan="2" style="text-align:center; vertical-align:middle">Inter-Data Centers</th>
  </tr>
  <tr>
    <th style="text-align:center; vertical-align:middle">Large Tensors</th>
    <th style="text-align:center; vertical-align:middle">Tiny Tensors</th>
    <th style="text-align:center; vertical-align:middle">Large Tensors</th>
    <th style="text-align:center; vertical-align:middle">Tiny Tensors</th>
  </tr>
  <tr>
    <th style="text-align:center; vertical-align:middle">Bi-Sparse</th>
    <td style="text-align:center; vertical-align:middle">FP32, Dense</td>
    <td style="text-align:center; vertical-align:middle">FP32, Dense</td>
    <td style="text-align:center; vertical-align:middle">FP32, Sparse</td>
    <td style="text-align:center; vertical-align:middle">FP32, Dense</td>
  </tr>
  <tr>
    <th style="text-align:center; vertical-align:middle">FP16</th>
    <td style="text-align:center; vertical-align:middle">FP16, Dense</td>
    <td style="text-align:center; vertical-align:middle">FP16, Dense</td>
    <td style="text-align:center; vertical-align:middle">FP16, Dense</td>
    <td style="text-align:center; vertical-align:middle">FP16, Dense</td>
  </tr>
  <tr>
    <th style="text-align:center; vertical-align:middle">MPQ</th>
    <td style="text-align:center; vertical-align:middle">FP32, Dense</td>
    <td style="text-align:center; vertical-align:middle">FP16, Dense</td>
    <td style="text-align:center; vertical-align:middle">FP32, Sparse</td>
    <td style="text-align:center; vertical-align:middle">FP16, Dense</td>
  </tr>
</table>

For details on how to classify large and tiny tensors, please refer to the [instruction of Bi-Sparse](#bidirectional-gradient-sparsification). The demo code for using MPQ is given below:

```python
import os
import mxnet as mx

# Define the threshold to classify large and tiny tensors, here, the threshold
# is the same as that in Bidirectional Gradient Sparsification.
size_lower_bound = int(os.getenv('MXNET_KVSTORE_SIZE_LOWER_BOUND', 2e5))

# Initialize distributed kvstore in synchronous mode.
kvstore_dist = mx.kv.create("dist_sync")
is_master_worker = kvstore_dist.is_master_worker

# Master worker enables bidirectional gradient sparsification on the global parameter server.
if is_master_worker:
    kvstore_dist.set_gradient_compression({"type": "bsc", "threshold": compression_ratio})

# Initialize kvstore space on parameter servers to store model parameters or gradients.
# Create 32-bit space for large tensors and 16-bit space for tiny tensors.
for idx, param in enumerate(net_params):
    init_buff = param.data() if param.data().size > size_lower_bound \
        else param.data().astype('float16')
    kvstore_dist.init(idx, init_buff)
    if is_master_worker: continue
    kvstore_dist.pull(idx, init_buff)
    param.set_data(init_buff.astype('float32'))

for epoch in range(num_epochs):
    for _, batch in enumerate(train_iter):
        # Perform forward and backward propagation to calculate gradients.
        ...
        # Synchronize gradients for gradient aggregation.
        for idx, param in enumerate(net_params):
            if param.grad_req == "null": continue
            # Push / pull large tensors in 32 bits, but tiny tensors in 16 bits.
            grad_buff = param.grad() if param.grad().size > size_lower_bound \
                else param.grad().astype('float16')
            kvstore_dist.push(idx, grad_buff, priority=-idx)
            kvstore_dist.pull(idx, grad_buff, priority=-idx)
            # Convert received gradient tensors back to 32 bits.
            param.grad()[:] = grad_buff.astype('float32')
        # Use aggregated gradients to update local model parameters.
        trainer.step(num_all_workers * batch_size)
        # Put gradients to zero manually.
        for param in net_params:
            param.zero_grad()
```

You can also find them in [`examples/cnn_mpq.py`](https://github.com/INET-RC/GeoMX/blob/main/examples/cnn_mpq.py) and run this demo by executing `scripts/xpu/run_mixed_precision.sh`, where `xpu` should be `cpu` or `gpu`.

### Differential Gradient Transmission
Differential Gradient Transmission (DGT) is an optimized transmission protocol for distributed machine learning tasks. Leveraging the tolerance of gradient descent algorithms towards partial parameter loss, this protocol transfers gradients across multiple channels, each with distinct levels of reliability and priority, contingent on their respective contributions to model convergence. Through these prioritized channels, critical gradients receive precedence in transmission, while other non-important gradients are transmitted with lower priority and reliability. This helps to reduce tail latency and thus reduce the end-to-end transmission delay of parameter synchronization. (Refer to [this paper](https://drive.google.com/file/d/1IbmpFybX_qXZM2g_8BrcD9IF080qci94/view) for more details and [this repo](https://github.com/zhouhuaman/dgt) for individual use.)

To enable DGT, set the following environment variables:

```shell
ENABLE_DGT = 2  # whether to enable DGT, use value 2 for DGT instead of value 1
DMLC_UDP_CHANNEL_NUM = 3  # number of transmission channels
DMLC_K = 0.8  # compression ratio
ADAPTIVE_K_FLAG = 1  # set value K adaptively
```

Use the demo script `scripts/xpu/run_dgt.sh` to try it!

### TSEngine
To solve the communication in-cast issue typically associated with centralized parameter servers, GeoMX incorporates TSEngine, an adaptive communication scheduler designed for efficient communication overlay in WANs. TSEngine dynamically optimizes the topology overlay and communication logic among the training nodes in response to real-time network conditions. This adaptive scheduler shows significant advantages over existing communication patterns in terms of system efficiency, communication, as well as scalability. (Refer to [this paper](https://drive.google.com/file/d/1ELfApVoCA8WCdOe3iBe-VreLJCSD7r8r/view) for more details and [this repo](https://github.com/zhouhuaman/TSEngine) for individual use.)

Similar to DGT, only a few environment variables are required to enable TSEngine:

```shell
ENABLE_INTER_TS = 1  # whether to enable TSEngine within the data center
ENABLE_INTRA_TS = 1  # whether to enable TSEngine between data centers
MAX_GREED_RATE_TS = 0.9  # perform exploration with a probability of 10%
```

Use the demo script `scripts/xpu/run_tsengine.sh` to try it!

> If `ENABLE_INTER_TS` is used, then TSEngine is enabled across data centers. Instead, if `ENABLE_INTRA_TS` is used, then TSEngine is enabled inside the data center. In this example, both `ENABLE_INTER_TS` and `ENABLE_INTRA_TS` are enabled, but we can also choose to enable only one. 

### Priority-based Parameter Propagation
In conventional implementations, the gradient synchronization at round $r$ does not overlap with the forward propagation at round $r+1$, because the forward propagation relies on the completion of gradient synchronization. To improve system efficiency, GeoMX integrates the Priority-based Parameter Propagation (P3) scheduler, which prioritizes the transmission of shallow-layer gradients. This setup enables overlapping between forward propagation and gradient synchronization, allowing earlier execution of forward propagation for the next round, thereby accelerating distributed training. (See [this paper](https://arxiv.org/pdf/1905.03960.pdf) for more details and [this repo](https://github.com/anandj91/p3) for individual use.)

To enable P3, only one environment variable is required:

```shell
ENABLE_P3 = 1  # whether to enable P3
```

Use the demo script `scripts/xpu/run_p3.sh` to try it!

### Multi-Server Load Balancing
GeoMX supports a balanced distribution of workload, including traffic, storage, and computation, across multiple global parameter servers. By preventing any single server from becoming a bottleneck, Multi-Server Load Balancing (MultiGPS) significantly enhances efficiency, scalability, and overall performance of our GeoMX system.

To enable MultiGPS, set `DMLC_NUM_GLOBAL_SERVER` and some `DMLC_NUM_SERVER` to an integer greater than 1.

```shell
# In the central party:
# For the global scheduler
DMLC_NUM_GLOBAL_SERVER = 2
# For the global server 0
DMLC_NUM_GLOBAL_SERVER = 2
DMLC_NUM_SERVER = 2
# For the global server 1
DMLC_NUM_GLOBAL_SERVER = 2
DMLC_NUM_SERVER = 2
# For the master worker
DMLC_NUM_SERVER = 2
# For the local scheduler in the central party
DMLC_NUM_SERVER = 2

# In the other parties:
# For the local server
DMLC_NUM_GLOBAL_SERVER = 2
```

Use the demo script `scripts/xpu/run_multi_gps.sh` to try it!