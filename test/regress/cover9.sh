set -xe

pwd
which nvc

# Exclude all uncovered things -> Test valid exclude file
nvc -a $TESTDIR/regress/cover9.vhd -e --cover=all cover9 -r
nvc --cover-report --exclude-file $TESTDIR/regress/data/cover9_ef1.txt \
    -o html work/_WORK.COVER9.elab.covdb 2>&1 | tee out.txt

# Test invalid command in exclude file
nvc --cover-report --exclude-file $TESTDIR/regress/data/cover9_ef2.txt \
    -o html1 work/_WORK.COVER9.elab.covdb 2>&1 | tee -a out.txt

# Test invalid bin name
nvc --cover-report --exclude-file $TESTDIR/regress/data/cover9_ef3.txt \
    -o html2 work/_WORK.COVER9.elab.covdb 2>&1 | tee -a out.txt

# Bin placed for command
nvc --cover-report --exclude-file $TESTDIR/regress/data/cover9_ef4.txt \
    -o html3 work/_WORK.COVER9.elab.covdb 2>&1 | tee -a out.txt

# Hierarchy missing
nvc --cover-report --exclude-file $TESTDIR/regress/data/cover9_ef5.txt \
    -o html4 work/_WORK.COVER9.elab.covdb 2>&1 | tee -a out.txt

# Adjust output to be work directory relative
sed -i -e "s/[^ ]*regress\/data\//data\//g" out.txt

if [ ! -f html/index.html ]; then
  echo "missing coverage report"
  exit 1
fi

diff -u $TESTDIR/regress/gold/cover9.txt out.txt
