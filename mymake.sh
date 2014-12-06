#########################################################################
# File Name: mymake.sh
# Author: Ren Zhen
# mail: renzhengeek@gmail.com
# Created Time: 2014年04月27日 星期日 10时18分13秒
#########################################################################
#!/bin/bash

if [ $# -gt 0 ]; then
	echo "remake..."
	make env
	python scripts/slowload.py --symbols
	make detach KERNEL=1
	make wrappers KERNEL=1
	make clean KERNEL=1 ; make all KERNEL=1 GR_CLIENT=bounds_checker
	echo "end of remake"
fi

python scripts/fastload.py --remote vm
