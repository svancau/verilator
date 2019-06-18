library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pwm_top is
    port (
	clk : in std_logic;
	duty : in unsigned(11 downto 0);
	output : out std_logic_vector(2 downto 0)
    );
end entity pwm_top;

architecture rtl of pwm_top is
  constant pwm_size : integer := 12;
begin

pwm0 : entity work.pwm
generic map(
	size => pwm_size)
port map(
	clk => clk,
	duty => duty,
	output => output(0));

pwm1 : entity work.pwm
generic map(
	size => pwm_size)
port map(
	clk => clk,
	duty => duty,
	output => output(1));

pwm2 : entity work.pwm
generic map(
	size => pwm_size)
port map(
	clk => clk,
	duty => duty,
	output => output(2));
end architecture rtl;
