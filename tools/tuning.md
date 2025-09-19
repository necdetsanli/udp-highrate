 
# Tuning Notes
 
For very high packet rates, consider:
 
```
sudo sysctl -w net.core.rmem_max=268435456
sudo sysctl -w net.core.wmem_max=268435456
sudo sysctl -w net.core.netdev_max_backlog=16384
sudo sysctl -w net.ipv4.udp_mem='8388608 12582912 16777216'
sudo sysctl -w net.ipv4.udp_rmem_min=16384
sudo sysctl -w net.ipv4.udp_wmem_min=16384
```
 
Pinning server to a CPU core and running multiple instances with `--reuseport` can further scale.
 
 