#!/bin/sh

LINKS_FILE=$ARCH_DIR/archlinks.cfg
ARCH_INC=$ARCH_DIR/include
ARCH_SRC=$ARCH_DIR/src
COMMON_INC=$ARCH_COM_DIR/include
COMMON_SRC=$ARCH_COM_DIR/src

show_help()
{
    appname=`basename $0`
    echo "Usage: $appname <create|clear>"
    exit 1
}

[ ! -e $LINKS_FILE ] && exit 0
[ -z "$1" ] || [ "$1" != "create" ] || [ "$1" != "clear" ] || show_help

cat $LINKS_FILE | \
    while read src dst; do
       [ "$src" = "#" ] && continue

       srcf=$ARCH_COM_DIR/$src
       dstf=$ARCH_DIR/$dst

       if [ ! -e $srcf ]; then
           echo "Source $srcf doesn't exists!" > /dev/stderr
           exit 1
       fi
       if [ -e $dstf ]; then
           if [ "$1" = "create" ]; then
               echo "Destination $dstf(src = $srcf) already exists!" > /dev/stderr
               exit 1
           else
               if [ ! -h $dstf ]; then
                   echo "Destination $dstf is not a symlink. Can't remove it!" > /dev/stderr
                   exit 1
               fi

               rm -f $dstf
           fi
       else
           [ "$1" = "create" ] && ln -s $srcf $dstf
       fi
    done

exit 0