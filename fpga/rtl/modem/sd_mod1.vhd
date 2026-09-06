library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sd_mod1 is
    generic ( IW : integer := 12 );
    port (
        clk  : in  std_logic;
        rst  : in  std_logic;
        en   : in  std_logic;
        din  : in  signed(IW-1 downto 0);
        dout : out std_logic
    );
end entity;

architecture rtl of sd_mod1 is
    constant ACCW : integer := IW + 3;
    constant FULL : signed(ACCW-1 downto 0) := to_signed(2**(IW-1), ACCW);
    signal acc : signed(ACCW-1 downto 0) := (others => '0');
begin
    process(clk)
    begin
        if rising_edge(clk) then
            if rst = '0' then
                acc  <= (others => '0');
                dout <= '0';
            elsif en = '1' then
                if acc >= 0 then
                    dout <= '1';
                    acc  <= acc + resize(din, ACCW) - FULL;
                else
                    dout <= '0';
                    acc  <= acc + resize(din, ACCW) + FULL;
                end if;
            end if;
        end if;
    end process;
end architecture;
