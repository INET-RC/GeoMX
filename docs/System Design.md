## GeoMX

Shown below is a general demonstration of `GeoMX` infrastructure.

![GeoMX Overview](./src/image/../images/GeoMX%20Overview.png)

Clusters are divided into two categories, the central and the participating. Each owns a `Parameter Server` and some `Workers` while the central PS named `Global Server` aggregates models, gradients or model updates from all other participating clusters. Both locally and globally, nodes work in a traditional `Server ↔ Worker` pattern. Unlike other single-layer prameter server framework, GeoMX takes privacy into consideration, meanwhile reducing communication overheads via WAN.

## BSC
`BSC` adopts top-k sparsification, which sort the gradient matrix and only top kth largest gradients are transmitted. To avoid information loss, gradients left over from last round of sparsification are collected and corrected in store for the next round.

![BSC Overview](./src/image/../images/BSC%20Overview.png)

## DGT
`DGT` ranks all the sub-blocks in the k-th tensor according to their contribution, and classifies the gradients of top-p% sub-blocks as important and the rest as unimportant. Then, DGT schedules the important gradients to the channel with reliable transmission protocol (e.g. TCP) to provide model convergence guarantee, instead, the unimportant gradients are scheduled to channels with unreliable transmission protocol (e.g. UDP) to minimum transmission delay.

![DGT Overview](./src/image/../images/DGT%20Overview.png)

## TSEngine
The core idea of TSEngine is to perform efﬁcient communication overlay over the parameter server and workers through dynamic communication scheduling. A global coordinator and local agents are deployed under the parameter server and workers. The global coordinator schedules the communication logic between local agents in an online manner.

![TS Overview](./src/image/../images/TS%20Overview.png)


## P3
According to the knowledge of Forward Propagation, the earlier layer of the neural network in the Push stage are more important, and the earlier it should be sent out. Therefore, in the communication link of each iteration, the importance of different coflows is different. Hence, coflow of high priority must be arranged immediately the next time it is sent. Therefore, changing the FIFO strategy to arrange transmission based on priority will overlap Forward Propagation and Pull progresses.

![P3 Overview](./src/image/../images/P3%20Overview.png)