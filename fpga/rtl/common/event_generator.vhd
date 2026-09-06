library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity event_generator is
    generic (
        General_CLK     : integer := 36_000_000;   -- SX1255 CLK_OUT
        Event_After_FIR : integer := 400_000;      -- RRC cikis hizi
        INTERP          : integer := 4             -- RRC interpolasyon orani
    );
    port (
        clk          : in  std_logic;
        rst          : in  std_logic;
        o_before_FIR : out std_logic;              -- sembol hizi (100 kHz)
        o_After_FIR  : out std_logic               -- RRC hizi   (400 kHz)
    );
end entity event_generator;

architecture rtl of event_generator is

    constant AFTER_FIR_MAX : integer := (General_CLK / Event_After_FIR) - 1;
    constant SLOW_MAX      : integer := INTERP - 1;

    signal clk_cnt  : integer range 0 to AFTER_FIR_MAX := 0;
    signal slow_cnt : integer range 0 to SLOW_MAX      := 0;
    signal bf_ff, af_ff : std_logic := '0';

begin

    -- VHDL'de tamsayi bolme sessizce asagi yuvarlar; bolme tam
    -- cikmazsa frekans kayar ve fark etmezsin.
    assert (General_CLK mod Event_After_FIR) = 0
        report "event_generator: General_CLK, Event_After_FIR'e tam bolunmuyor"
        severity failure;

    assert INTERP > 0
        report "event_generator: INTERP pozitif olmali"
        severity failure;

    process(clk)
    begin
        if rising_edge(clk) then
            if rst = '0' then
                clk_cnt  <= 0;
                slow_cnt <= 0;
                bf_ff    <= '0';
                af_ff    <= '0';
            else
                bf_ff <= '0';               -- varsayilan: tek cevrimlik darbe
                af_ff <= '0';

                if clk_cnt = AFTER_FIR_MAX then
                    clk_cnt <= 0;
                    af_ff   <= '1';         -- RRC cikis darbesi

                    if slow_cnt = SLOW_MAX then
                        slow_cnt <= 0;
                        bf_ff    <= '1';    -- sembol darbesi
                    else
                        slow_cnt <= slow_cnt + 1;
                    end if;
                else
                    clk_cnt <= clk_cnt + 1;
                end if;
            end if;
        end if;
    end process;

    o_before_FIR <= bf_ff;
    o_After_FIR  <= af_ff;

end architecture rtl;