# CPU benchmark report

Derived from the paired rows in `raw.csv`; no inferred hardware measurements.

Times are CLI engine-run elapsed milliseconds. Brackets show observed min/max, not confidence intervals.
The ratio is median Tick time / median Event time; values above one favor Event. Trial order alternates.

| Model/workload | Pairs | Tick median [min, max] ms | Event median [min, max] ms | Tick/Event |
| --- | ---: | ---: | ---: | ---: |
| dense / reduction (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | 7 | 0.069 [0.069, 0.069] | 0.094 [0.094, 0.094] | 0.731 |
| dense / relay (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | 7 | 0.534 [0.525, 0.622] | 0.655 [0.653, 0.667] | 0.815 |
| dense / scan (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | 7 | 0.418 [0.415, 0.426] | 0.418 [0.412, 0.440] | 0.999 |
| sparse / reduction (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | 7 | 2.147 [2.068, 2.193] | 0.094 [0.094, 0.097] | 22.810 |
| sparse / relay (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | 7 | 27.296 [27.121, 27.388] | 0.889 [0.885, 1.009] | 30.714 |
| sparse / scan (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | 7 | 29.201 [28.782, 33.176] | 0.413 [0.412, 0.468] | 70.704 |

Scheduler columns are medians. Their definitions differ by executor; simulated ticks describe the model, not host time.
RSS is the process peak including program construction and warmups, in MiB; zero means unavailable.

| Model/workload | Engine | Instructions | Final tick | Scheduler events | Clock steps | PE checks | Peak RSS MiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| dense / reduction (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | tick | 4119 | 1041 | 4119 | 1042 | 8336 | 1.25 |
| dense / reduction (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | event | 4119 | 1041 | 4120 | 1042 | 8336 | 1.25 |
| dense / relay (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | tick | 20484 | 6152 | 20484 | 6153 | 49224 | 2.09 |
| dense / relay (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | event | 20484 | 6152 | 22531 | 6153 | 49224 | 2.09 |
| dense / scan (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | tick | 12311 | 7697 | 12311 | 7698 | 61584 | 1.44 |
| dense / scan (P=4, N=2048, cap=1, seed=7, latency=1/1/1) | event | 12311 | 7697 | 12312 | 7698 | 61584 | 1.44 |
| sparse / reduction (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | tick | 4119 | 83520 | 4119 | 83521 | 668168 | 1.25 |
| sparse / reduction (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | event | 4119 | 83520 | 4120 | 1042 | 8336 | 1.25 |
| sparse / relay (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | tick | 20484 | 852960 | 20484 | 852961 | 6.82369e+06 | 2.88 |
| sparse / relay (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | event | 20484 | 852960 | 22528 | 12289 | 98312 | 2.09 |
| sparse / scan (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | tick | 12311 | 738880 | 12311 | 738881 | 5.91105e+06 | 1.56 |
| sparse / scan (P=4, N=2048, cap=1, seed=7, latency=32/128/256) | event | 12311 | 738880 | 12312 | 7698 | 61584 | 1.44 |

Exact commands, latency parameters, source/binary hashes, declared compiler flags, load samples, and timing boundaries
are retained in `raw.csv` and adjacent `metadata.json`. These results compare this custom simulator on one host CPU.
