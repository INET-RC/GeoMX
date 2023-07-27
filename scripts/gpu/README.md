This folder contains some scripts for running examples in <code>examples/</code>, which should **run on one machine with at least one gpu**.

1. To try the vanilla **Hierarchical Parameter Server** framework (i.e., **HiPS**), run <code>bash run_vanilla_hips.sh</code>
2. To try the **Mixed-Synchronization** algorithm (i.e., **Mixed-Sync**), run <code>bash run_mixed_sync.sh</code>
3. To try the **Hierarchical FedAvg** algorithm (i.e., **HierFAVG**), run <code>bash run_hierfavg_sync.sh</code>
4. To try the **Bidirectional Gradient Compression** technique (i.e., Bi-Sparse Compression, **BSC**), run <code>bash run_bisparse_compression.sh</code>
5. To try the **Differentiated Gradient Transmission** protocol (i.e., **DGT**), run <code>bash run_dgt.sh</code>
6. To try **P3**, run <code>bash run_p3.sh</code>

These scripts will start 12 nodes, including: 

* Within the Central Party: A global scheduler, a global server, a master worker, and a scheduler.
* Within the Party A: A scheduler, a server, and two workers.
* Within the Party B: A scheduler, a server, and two workers.
