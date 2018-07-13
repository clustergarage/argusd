# fimd
A daemon process that runs on each node of a cluster watching files for FIM compliance

```
oc edit scc privileged
# add under users:
# - system:serviceaccount:kube-system:fim-admin
```
