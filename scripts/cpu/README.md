This folder contains some scripts for running examples in <code>examples/</code>, which can run on one machine with only cpus.

1. To try the vanilla **Hierarchical Parameter Server** framework (i.e., **HiPS**), run <code>bash run_vanilla_hips.sh</code>
2. To try the **Mixed-Synchronization** algorithm (i.e., **MixedSync**), run <code>bash run_mixed_sync.sh</code>
3. To try the **Hierarchical Frequency Aggregation** algorithm (i.e., **HFA**), run <code>bash run_hfa_sync.sh</code>
4. To try the **Bidirectional Gradient Compression** technique (i.e., **Bi-Sparse Compression** or **BSC**), run <code>bash run_bisparse_compression.sh</code>
5. To try the **Differentiated Gradient Transmission** protocol (i.e., **DGT**), run <code>bash run_dgt.sh</code>
6. To try the **Priority-based Parameter Propagation** scheduler (i.e., **P3**), run <code>bash run_p3.sh</code>
7. To try the **TSEngine** scheduler, run <code>bash run_tsengine.sh</code>
8. To try the **Mixed-Precision Quantization** technique (i.e., MPQ), run <code>bash run_mixed_precision.sh</code>
9. To try the **Low-Precision Quantization** technique (i.e., FP16), run <code>bash run_fp16.sh</code>

These scripts will start 12 nodes, including: 

* Within the Central Party: A global scheduler, a global server, a master worker, and a scheduler.
* Within the Party A: A scheduler, a server, and two workers.
* Within the Party B: A scheduler, a server, and two workers.
