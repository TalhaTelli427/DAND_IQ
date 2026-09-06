--------------------------------------------------------------------------------
-- prbs_byte_gen.vhd
--
-- 8-bit PRBS byte ureteci. Serbest kosar (her saat cevriminde yeni byte).
--
--   Polinom : x^15 + x^14 + 1
--   Periyot : 32767 bit  (~4095 byte)
--
-- Framer ne zaman okursa o anki degeri alir. Okuma hizi (25 kHz) ile
-- uretim hizi (36 MHz) arasinda 1440 kat fark oldugu icin her okumada
-- tamamen farkli bir byte gelir.
--
-- Reset AKTIF DUSUK.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity prbs_byte_gen is
    generic (
        data_width : natural := 8
    );
    port (
        clk      : in  std_logic;
        rstn     : in  std_logic;
        data_out : out std_logic_vector(data_width-1 downto 0)
    );
end entity prbs_byte_gen;

architecture rtl of prbs_byte_gen is

    constant C_SEED : std_logic_vector(14 downto 0) := "101010101010101";

    signal lfsr : std_logic_vector(14 downto 0) := C_SEED;

    -- Bir adim ilerlet: x^15 + x^14 + 1
    function nxt (r : std_logic_vector(14 downto 0))
                  return std_logic_vector is
    begin
        return r(13 downto 0) & (r(14) xor r(13));
    end function;

begin

    process (clk)
        variable v : std_logic_vector(14 downto 0);
        variable b : std_logic_vector(data_width-1 downto 0);
    begin
        if rising_edge(clk) then
            if rstn = '0' then
                lfsr     <= C_SEED;
                data_out <= (others => '0');
            else
                v := lfsr;
                for i in data_width-1 downto 0 loop
                    v    := nxt(v);
                    b(i) := v(14);
                end loop;
                lfsr     <= v;
                data_out <= b;
            end if;
        end if;
    end process;

end architecture rtl;
