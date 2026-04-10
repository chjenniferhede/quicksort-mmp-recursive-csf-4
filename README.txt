CONTRIBUTIONS

Independent work

REPORT

Test run with threshold 2097152

real    0m0.406s

Test run with threshold 1048576

real    0m0.224s

Test run with threshold 524288

real    0m0.164s

Test run with threshold 262144

real    0m0.150s

Test run with threshold 131072

real    0m0.142s

Test run with threshold 65536

real    0m0.110s

Test run with threshold 32768

real    0m0.114s

Test run with threshold 16384

real    0m0.123s

As the threshold decreases, more recursive partitions are sorted in parallel child processes, so the total wall-clock time drops at first. The best time here is around threshold 65536 because there is enough parallel work to use multiple cores effectively. Below that point, performance gets slightly worse because process creation, scheduling, and waiting overhead start to outweigh the benefit of adding more parallel tasks.