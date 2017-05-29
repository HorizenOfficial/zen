#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

ZEND=${ZCASHD:-$SRCDIR/zend}
ZENCLI=${ZCASHCLI:-$SRCDIR/zen-cli}
ZENTX=${ZCASHTX:-$SRCDIR/zen-tx}

[ ! -x $ZEND ] && echo "$ZEND not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
ZECVER=$($($ZENCLI --version | head -n1 | awk -F'[ -]' '{ print $5, $6 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for zend if --version-string is not set,
# but has different outcomes for zen-cli.
echo "[COPYRIGHT]" > footer.h2m
$ZEND --version | sed -n '1!p' >> footer.h2m

for cmd in $ZEND $ZENCLI $ZENTX; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${ZECVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${ZECVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
