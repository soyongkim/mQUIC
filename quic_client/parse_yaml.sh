# Copyright 2016 By Hojin Choi
# You should source this file as "YAML=../config/settings.yaml source parse_yaml.sh"

if test -z "$YAML"; then
	echo "YAML is not set"
	exit 1
fi

if test -z "$LEADSPACE"; then
	LEADSPACE="  "
fi
if test -z "$FIELDSP"; then
	FIELDSP="_"
fi

if test -z "$ENV"; then
	ENV="default"
fi

TMPSH="/tmp/tmp.$$.sh"

#Remove white space from IFS, we use only new line character
lastdepth="0"
while IFS="\n" read line
do
	depth=`echo "$line" | sed -e "s;$LEADSPACE;@;g" | sed -r -e 's;^(@*).*;\\1;' | wc -c`
	read k v <<<"$line"
	k=$(echo $k| tr -d ':')
	keys[$depth]="$k"
	if test "$lastdepth" -ne "$depth"; then
		lastdepth=$depth
	fi
	unset keys[$(($lastdepth+1))]
	unset keys[$(($lastdepth+2))]
	unset keys[$(($lastdepth+3))]
	unset keys[$(($lastdepth+4))]
	unset keys[$(($lastdepth+5))]
	unset keys[$(($lastdepth+6))]
	unset keys[$(($lastdepth+7))]
	unset keys[$(($lastdepth+8))]

	if test "$ENV" != "${keys[1]}"; then
		continue
	fi

	if test -z "$v"; then
		continue
	fi

	v=$(echo $v| tr -d '"')
	read key <<<"$(echo ${keys[@]:2} | tr ' ' $FIELDSP)"
	echo $key=$v
done < "$YAML" > "$TMPSH"

. "$TMPSH"

rm -f "$TMPSH"