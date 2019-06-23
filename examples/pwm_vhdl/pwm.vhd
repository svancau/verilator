library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pwm is
    generic (size : integer := 12);
    port (
	clk : in std_logic;
	duty : in unsigned(size-1 downto 0);
	output : out std_logic
    );
end entity pwm;

architecture rtl of pwm is
    signal counter : unsigned(size-1 downto 0) := (others => '0');
begin
    process(clk)
    begin
        if rising_edge(clk) then
	    counter <= counter + 1;
	    if counter < duty then
              output <= '1';
	    else
	      output <= '0';
	    end if;
	end if;

    end process;

end architecture rtl;
