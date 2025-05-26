# redis-server
Making a redis server from scratch in C++

### TODO:
- Implement multi-processed architecture using Linux fork() syscall
- Implement the Server as a base class using singleton design pattern and have the 3 concurrency architectures as derived classes
- Benchmark the 3 implementations against each other using Boost and GoogleTest
