####### Test data ########
## run with gawk -l libgawk_vis.so -f vis_test.awk vis_test.awk
## the "table" below is the test data - this it to avoid quoting issues of the expected output data
## input             |  encoded
##-------------------|-------------
#| unchanged         | unchanged
#| ./a/b/foo.bar     | ./a/b/foo.bar
#| /my file_test     | /my\040file_test
#| *?[#              | \052\077\133\043
#| !"$&'();<>[\]^`~  | \041\042\044\046\047\050\051\073\074\076\133\134\135\136\140\176
#| こんにちは          | \343\201\223\343\202\223\343\201\253\343\201\241\343\201\257
#| โลก               | \340\271\202\340\270\245\340\270\201
#| 🤯                | \360\237\244\257
#| abcd              | \141\142\143\144  | dcba
#| !"$&'();<>[\]^`~  | !"$&'();<>[\]^`~  | xyz

## can't provide multi-byte in printable set - returns unaltered
#| 🤯  | 🤯  | 🤯
#################################################################
BEGIN {
    FS="\\|"
    Err_template = "'%s' failed.\nExpected: '%s'\nActual:   '%s'\n"
    Test_nr = 0
}

function assert_endec(input, encoded, printable_set,    actual, decoded) {
    if (printable_set == "") {
        actual = vis::enc(input)
    } else {
        actual = vis::enc(input, printable_set)
    }

    if (actual != encoded) {
        Enc_fail[Test_nr] = sprintf(Err_template, input, encoded, actual)
    }
    # decode
    decoded = vis::dec(encoded)
    if ( decoded != input) {
        Dec_fail[Test_nr] = sprintf(Err_template, encoded, input, decoded)
    }
}

$1 == "#" {
    # trim leading/trailing whitespace
    Test_nr++
    for (i = 2; i <= NF; i++) {
        gsub(/^[ \t]+|[ \t]+$/, "", $i)
    }
    assert_endec($2, $3, $4)
}
$0 ~ /^#+$/ {
    # end of test data
    # testing pipe separately since FS is |
    assert_endec("|", "\\174" )
    nextfile
}
{ }
END {
    enc_fails = 0
    for (i in Enc_fail) {
        enc_fails++
        print("vis::enc", Enc_fail[i]) > "/dev/stderr"
    }
    dec_fails = 0
    for (i in Dec_fail) {
        dec_fails++
        print(Dec_fail[i]) > "/dev/stderr"
    }
    printf("vis:enc %s/%s succeded\n", Test_nr - enc_fails, Test_nr)
    printf("vis:dec %s/%s succeded\n", Test_nr - dec_fails, Test_nr)
    exit(enc_fails + dec_fails)

}