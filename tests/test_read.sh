#!/usr/bin/env bats
# vim: sts=4 sw=4 et

setup()
{
    if [ -z "$BATS_TEST_TMPDIR" ]; then
        export BATS_TEST_TMPDIR=`mktemp -d`
        export TEMPDIR_CLEANUP=yes
    fi
}

teardown()
{
    if [ "$TEMPIR_CLEANUP" == "yes" && ! -z "$BATS_TEST_TMPDIR" ]; then
        rm -rf "$BATS_TEST_TMPDIR"
    fi
}

testbody()
{
    echo -n "- seq: $1
  name: Data
  data:
    f0: $1"
}

@test "dump seq" {
    result=$(./tll-read --dump-seq tests/read.dat)
    echo "$result"
    [ "$result" == "First seq: 0
Last seq: 9" ]
}

@test "init params" {
    result=$(./tll-read --dump-seq 'tests/read.dat;io=mmap')
    echo "[$result]"
    [ "$result" == "First seq: 0
Last seq: 9" ]
}

@test "init with proto" {
    result=$(./tll-read --dump-seq 'file://tests/read.dat;io=mmap')
    echo "[$result]"
    [ "$result" == "First seq: 0
Last seq: 9" ]
}

@test "open params" {
    result=$(./tll-read tests/read.dat -Oseq=9)
    echo "[$result]"
    [ "$result" == "$(testbody 9)" ]
}

@test ":count" {
    result=$(./tll-read -s :+1 tests/read.dat)
    echo "[$result]"
    [ "$result" == "$(testbody 0)" ]
}

@test "rev-count:" {
    result=$(./tll-read -s=-1: tests/read.dat)
    echo "[$result]"
    [ "$result" == "$(testbody 8)

$(testbody 9)" ]
}

@test "seq:count" {
    result=$(./tll-read -s 4:+2 tests/read.dat)
    echo "[$result]"
    [ "$result" == "$(testbody 4)

$(testbody 5)" ]
}

@test "seq:seq" {
    result=$(./tll-read -s 4:5 tests/read.dat)
    echo "[$result]"
    [ "$result" == "$(testbody 4)

$(testbody 5)" ]
}

@test "convert" {
    FN=$BATS_TEST_TMPDIR/copy.dat
    ./tll-convert tests/read.dat "$FN"
    result=$(./tll-read --dump-seq "$FN")
    echo "[$result]"
    [ "$result" == "First seq: 0
Last seq: 9" ]
}
