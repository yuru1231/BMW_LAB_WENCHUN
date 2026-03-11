config:
```
wenj@ubuntu-sns32:~/workspace/ns-3.43$ ./ns3 run "isl-leo-candidate \
--mode=d1_final \
--scenarioFolder=constellation-telesat-351-sats \
--simTime=30 \
--tStart=0 \
--tEnd=30 \
--dt=1 \
--planWindow=5 \
--refLat=25.0330 \
--refLon=121.5654 \
--elevDeg=20 \
--islMaxDistanceKm=3000 \
--gwIndex=0 \
--statsLevel=min \
--outDir=/tmp/d1_dynamic_30 \
--pcapDir=/tmp/d1_dynamic_30/pcap" 2>&1 | tee /tmp/d1_dynamic_30.log
```
`routing plan`
```
time_start	time_end	serving_sat	path	hop_count	status	gw_index	reason
0	          5	        -1		            -1	      NO_PATH 0	        no_candidate_in_window
5		        10	      -1		            -1	      NO_PATH	0	        no_candidate_in_window
10		      15        -1		            -1	      NO_PATH	0	        no_candidate_in_window
15		      20        -1		            -1	      NO_PATH	0	        no_candidate_in_window
20		      25        -1		            -1	      NO_PATH	0	        no_candidate_in_window
25		      30	      -1		            -1	      NO_PATH	0	        no_candidate_in_window
```

 `csv-graph`
- Elevation Histogram
 - x-asix: Elevation to GW (deg)
 - y-asix: Satellite count
<img width="777" height="458" alt="image" src="https://github.com/user-attachments/assets/63538b2e-1988-44bb-b450-3fd9f2d2bcfc" />
>  LEO routing / gateway visibility中，Elevation>20°非常少 不盡理想

- Hop Count Histogram
 - x-asix: Path Hops
 - y-asix: Number of candidate satellites
<img width="452" height="453" alt="image" src="https://github.com/user-attachments/assets/e996cafd-8ba8-4ff8-9da4-ac52f90e7554" />
> hop=0 ，candidate selection rule too loose


- Elevation vs Hop Scatter
 - x-asix: Elevation
 - y-asix: Hop count
<img width="770" height="461" alt="image" src="https://github.com/user-attachments/assets/d9384105-b407-498d-818a-62cf85659536" />
> 不論Elevation，皆沒有hop ->

- Mean Elevation per Satellite
 - x-asix: Satellite ID
 - y-asix: Mean elevation

<img width="759" height="460" alt="image" src="https://github.com/user-attachments/assets/3fcc18f9-c708-4ba2-903a-71c5d6f2644d" />
 > Find gateway anchor satellites

