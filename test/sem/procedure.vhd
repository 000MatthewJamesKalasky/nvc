package p is

    procedure foo(x : in integer; y : out integer);

    procedure yah is                    -- Error
    begin
        null;
    end procedure;

end package;

package body p is

    procedure foo(x : in integer; y : out integer) is
        variable i : integer;
    begin
        y := x + 1;
    end procedure;

    procedure bar(x : in integer; signal y : out integer) is
    begin
        y <= x + 1;
    end procedure;

    procedure yam is
    begin
        return;                         -- OK
        return 5;                       -- Error
    end procedure;

    procedure foo_wrap(y : out integer) is
    begin
        foo(5, y);
    end procedure;

    procedure has_def(x : in integer; y : in integer := 7) is
    begin
    end procedure;

    procedure calls_has_def is
    begin
        has_def(5);
    end procedure;

    procedure bad_def(x : in bit := 6) is
    begin
    end procedure;

    procedure bad_def2(x : in bit := '1'; y : in integer) is
    begin
    end procedure;

    procedure diff_types(x : in integer; y : in string) is
    begin
    end procedure;

    procedure test_named is
    begin
        diff_types(1, "foo");            -- OK
        diff_types(1, y => "bar");       -- OK
        diff_types(x => 1, y => "foo");  -- OK
        diff_types(y => "la", x => 6);   -- OK
        diff_types(y => "foo");          -- Error
        diff_types(y => "f", 6);         -- Error
    end procedure;

    procedure overload(x : in bit) is
    begin
    end procedure;

    procedure overload(x : in integer) is
    begin
    end procedure;

    procedure test_overload is
    begin
        overload('1');
        overload(1);
    end procedure;

    procedure test1(x : in integer; y : out integer) is
    begin
        y := y + 1;                     -- Error
        x := 6;
    end procedure;

    procedure test2(signal x : in bit) is
    begin
        -- These are errors according to LRM 93 section 2.1.1.2
        assert x'stable;
        assert x'quiet;
        assert x'transaction = '1';
        assert x'delayed(1 ns) = '1';
    end procedure;

    type int_ptr is access integer;

    procedure test3(constant x : inout int_ptr);  -- Error
    procedure test4(x : in int_ptr);  -- Error
    procedure test4(x : int_ptr);  -- Error
    procedure test4(x : out int_ptr);  -- OK

end package body;
