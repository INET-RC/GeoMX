## How Examples are Organized

Examples of 4 communication algorithms are based on `Convolutional Neural Networks` while examples of other machine learning algorithms are based on `Fully-synchronous Algorithm`. Please pay attention to the fact that due to different algorithm design motivations and some limitations of code-level implementation, except for the commonly used Fully-synchronous Algorithm, other communication algorithms may not be fully compatible with each of the machine learning algorithms or communication-efficient strategies. Additionally, only algorithms related with gradient descent will work with `BSC`. Please use with caution.

## List of Supported Algorithms

As mentioned, GeoMX supports **4 communication algorithms** and  (6 categories) **25 machine learning algorithms**. Here is a list of supported algorithms. 

- **Four communication algorithms**
    1. Fully-synchronous Algorithm
    2. Mix-synchronous Algorithm
    3. HierFAVG-styled Synchronous Algorithm
    4. DC-ASGD Asynchronous Algorithm

- **Nine gradient descent algorithms**
    1. Convolutional Neural Networks, CNN
    2. Recurrent Neural Network, RNN
    3. Reinforcement Learning
    4. Regions with CNN features
    5. Matrix Factorization
    6. Generative Adversarial Network，GAN
    7. Logistic Regression
    8. RankNet
    9. ListNet

- **Seven ensemble learning algorithms**
    1. Gradient Boosting Decision Tree, GBDT
    2. XGBOOST
    3. Random Forest
    4. GBrank
    5. LambdaMART
    6. RankBoost
    7. AdaRank

- **Three support vector algorithms**
    1. Support Vector Machine, SVM
    2. Support Vector Regression, SVR
    3. RankSVM

- **Two MapReduce algorithms**
    1. k-Nearest Neighbor, KNN
    2. KMeans

- **Two online learning algorithms**
    1. Follow the Moving Leader, FTML
    2. Follow the Regularized Leader, FTRL

- **Two incremental learning algorithms**
    1. Feature Extraction
    2. Fine Tune