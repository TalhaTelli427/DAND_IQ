library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity power_on_reset is
    Port (
        clk      : in  STD_LOGIC;
        rst_n_out: out STD_LOGIC
    );
end entity;

architecture RTL of power_on_reset is
    signal count : unsigned(3 downto 0) := (others => '0');
    signal rst_reg : std_logic := '0';
begin
    process(clk)
    begin
        if rising_edge(clk) then
            if count < 15 then
                count <= count + 1;
                rst_reg <= '0'; -- Active-Low Reset basýlý
            else
                rst_reg <= '1'; -- Reset kaldýrýldý, sistem çalýþýyor
            end if;
        end if;
    end process;

    rst_n_out <= rst_reg;
end architecture;