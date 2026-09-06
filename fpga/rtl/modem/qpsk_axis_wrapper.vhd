library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity qpsk_axis_wrapper is
    port (
        clk            : in  std_logic;
        rst            : in  std_logic;
        en_100k        : in  std_logic;
        data_i         : in  std_logic_vector(1 downto 0);
        
        -- I Kanalı AXI4-Stream Master
        m_axis_i_tdata : out std_logic_vector(15 downto 0);
        m_axis_i_tvalid: out std_logic;
        m_axis_i_tready: in  std_logic;
        
        -- Q Kanalı AXI4-Stream Master
        m_axis_q_tdata : out std_logic_vector(15 downto 0);
        m_axis_q_tvalid: out std_logic;
        m_axis_q_tready: in  std_logic
    );
end entity;

architecture rtl of qpsk_axis_wrapper is

    signal I_sig  : signed(11 downto 0);
    signal Q_sig  : signed(11 downto 0);
    signal vld_d1 : std_logic := '0';

begin

    u_mapper : entity work.QPSK_mapper
        port map (
            clk     => clk,
            rst     => rst,
            data_i  => data_i,
            en_100k => en_100k,
            I_o     => I_sig,
            Q_o     => Q_sig
        );

    -- Timing Alignment: 1 Clock Gecikmeli Valid
    process(clk)
    begin
        if rising_edge(clk) then
            if rst = '0' then
                vld_d1 <= '0';
            else
                vld_d1 <= en_100k;
            end if;
        end if;
    end process;

    -- Sign Extension: 12-bit datayı 16-bit standardına çek
    m_axis_i_tdata  <= std_logic_vector(resize(I_sig, 16));
    m_axis_q_tdata  <= std_logic_vector(resize(Q_sig, 16));

    m_axis_i_tvalid <= vld_d1;
    m_axis_q_tvalid <= vld_d1;

end architecture rtl;