# Configuration Guide
This guidance describes the environmental variables and hyperparameters needed to launch each optimization technique in our GeoMX system.

## Synchronization Algorithms
GeoMX currently supports two fundamental synchronization algorithms, i.e., the fully-synchronous algorithm, the mixed-synchronous algorithm, and an advanced algorithm, i.e., hierarchical frequency aggregation.

### Fully-synchronous Algorithm

Simply creating a `dist_sync` KVStore will make it.

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_sync")
```

### Mixed-synchronous Algorithm

Likewise, create a `dist_async` KVStore.

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_async")
```

### HierFAVG Synchronous Algorithm

HierFAVG-styled Synchronous Algorithm is generally identical with Fully-synchronous Algorithm while the former adds a few loops before an local iteration or a global iteration.

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_sync")
``` 

Some Environment Variables should be additionally set.

```shell
MXNET_KVSTORE_USE_HFA=0 # whether HierFAVG is enabled
MXNET_KVSTORE_HFA_K1=20 # loops before a local iteration
MXNET_KVSTORE_HFA_K2=10 # loops before a global iteration
```

### DC-ASGD Asynchronous Algorithm

To mitigate straggler problems brought by the asynchronous pattern, DCASGD optimizer is introduced.

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_async")
kvstore_dist.set_optimizer(mx.optimizer.DCASGD(learning_rate=learning_rate))
```

## Communication-efficient Strategies

> Caution: Strategies mentioned below are only compatible with `Fully-synchronous Algorithm`

### BSC

In `.py` files, define the correct gradient compression type and an appropriate compression ratio.

```python
kvstore_dist.set_gradient_compression({"type": "bsc", "threshold": ratio})
```

Remember to set the compression lower bound according to your deep neural networks as a Environment Variable.

```shell
MXNET_KVSTORE_SIZE_LOWER_BOUND=200000
```

### DGT

Only a few Environment Variables are needed here.

```shell
ENABLE_DGT=0 # whether to enable DGT. Attention, 2 is for enabled, not 1
DMLC_UDP_CHANNEL_NUM=3 # Channels
DMLC_K=0.8 # compression ratio
ADAPTIVE_K_FLAG=1 # set K adaptively
```

### TSEngine

Only a few Environment Variables are needed here.

```shell
ENABLE_INTER_TS=0 # whether to enable locally
ENABLE_INTRA_TS=0 # whether to enable globally
MAX_GREED_RATE_TS=0.9
```

### P3

Only an Environment Variables are needed here.

```shell
ENABLE_P3=0 # whether to enable P3
```
