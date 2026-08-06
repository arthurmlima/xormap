clear all;
clc
K=32;
l=0;





[a]=demo_chunks(K);
%M=merge_cells_append_multi(a,b,c);
a(:,1)=[];
[M m1 m2]=extract_3section(a,K,l);

a = fliplr(a);
M = fliplr(M);

pretty_func(M)

%m1 = fliplr(m1);
%m2 = fliplr(m2);
%M = fill_blanks(M, m1, m2);
%pretty_func(M)

% for even k even.
%M = fill_missing_pair(M, K);

%pretty_func(M);





filename=sprintf('Ben_xormap_%d.vhd',K);
         
% Open the file for writing
fileID = fopen(filename, 'w');

% Check if the file opened successfully
if fileID == -1
    error('Failed to open the file for writing.');
end

% Write VHDL code to the file
fprintf(fileID, 'library ieee;\n');
fprintf(fileID, 'use ieee.std_logic_1164.all;\n\n');

fprintf(fileID, 'entity Ben_xormap_%d is\n',K);
fprintf(fileID, '  port (\n');
fprintf(fileID, '    clk  : in  std_logic;\n');
fprintf(fileID, '    rst  : in  std_logic;\n');
fprintf(fileID, '    load : in  std_logic;\n');
fprintf(fileID, '    en   : in  std_logic;\n');
fprintf(fileID, '    a    : in  std_logic_vector(%d downto 0);\n', K-1);
fprintf(fileID, '    s    : out std_logic_vector(%d downto 0)\n', K-1);
fprintf(fileID, '  );\n');
fprintf(fileID, 'end Ben_xormap_%d;\n\n',K);

fprintf(fileID, 'architecture rtl of Ben_xormap_%d is\n',K);
fprintf(fileID, '  signal x_reg  : std_logic_vector(%d downto 0) := (others => ''0'');\n', K-1);
fprintf(fileID, '  signal x_next : std_logic_vector(%d downto 0);\n', K-1);
fprintf(fileID, '\n');

[nRows, nCols] = size(M);

for j = 1:nRows
    for i = 1:nCols
        v = M{j,i};

        % Only accept a numeric 1x2 (or 2x1) vector like [7 8]
        if isnumeric(v) && isvector(v) && numel(v) == 2
            ou = v(:).';  % force row [y z]
            fprintf(fileID, 'signal s_%d_%d : std_logic;\n', ou(1), ou(2));
        end
    end
end

% [nRows, nCols] = size(b);
% for j = 1:nRows
%     for i = 1:nCols
%         v = b{j,i};
% 
%         % Only accept a numeric 1x3 (or 3x1) vector like [1 7 8]
%         if isnumeric(v) && isvector(v) && numel(v) == 3
%             ou = v(:).';  % force row [x y z]
%             fprintf(fileID, 'signal s_%d_%d : std_logic;\n', ou(2), ou(3));
%         end
%     end
% end
% 
% [nRows, nCols] = size(c);
% 
% for j = 1:nRows
%     for i = 1:nCols
%         v = c{j,i};
% 
%         % Only accept a numeric 1x3 (or 3x1) vector like [1 7 8]
%         if isnumeric(v) && isvector(v) && numel(v) == 3
%             ou = v(:).';  % force row [x y z]
%             fprintf(fileID, 'signal s_%d_%d : std_logic;\n', ou(2), ou(3));
%         end
%     end
% end



fprintf(fileID, 'begin\n');

[R, C] = size(M);
for i = C-K+1:C                  % loop columns
    for j = 1:R                  % loop rows within column (top -> bottom)

        v = M{j,i};

        % break column if null/empty
        if isempty(v)
            break;
        end

        % safety check (optional)
        if numel(v) ~= 2
            error("M{%d,%d} must be a 1x2 pair.", j, i);
        end

        x = v(1);
        y = v(2);

        % -------- Rule for [x,y] ----------
        fprintf(fileID,'    s_%d_%d <= x_reg(%d) xor x_reg(%d);\n\n', x, y, (x)-1, (y)-1 );
        % elseif d >= 2
        %     %fprintf(fileID,'    s_%d_%d <= (s_%d_%d) and ( ',  x, y,  x, y);
        %     va=[d-1 x y];
        %     [find_col, find_row] = find_prev_in_col_Cminus1(M, i-1, va);
        %     fprintf(fileID,'   s_%d_%d <= ', x, y);
        %     idx=0;
        %     while (find_row + idx <= R) && ~isempty(M{find_row + idx, find_col})
        %     o=M{find_row+idx,find_col};
        %     if idx==0 
        %         if isempty(M{find_row+idx+1,find_col}) %|| find_row+idx+1<R
        %             fprintf(fileID," '0';\n");                 
        %         else 
        %             fprintf(fileID,' (s_%d_%d) and (', o(2), o(3));
        %         end
        %     elseif find_row+idx+1==R+1
        %     fprintf(fileID,' (s_%d_%d));\n', o(2), o(3)); 
        % 
        %     elseif idx>0 && ((isempty(M{find_row+idx+1,find_col}))) %|| (find_row+idx+1==R+1))
        %      fprintf(fileID,' (s_%d_%d));\n', o(2), o(3));
        %     elseif idx>0 && ((~isempty(M{find_row+idx+1,find_col}))|| (find_row+idx+1<=R))  
        %     fprintf(fileID,' (s_%d_%d) xor', o(2), o(3)); 
        %     elseif idx>0 && ((~isempty(M{find_row+idx+1,find_col})) && (find_row+idx+1>R+1))  
        %     fprintf(fileID,' (s_%d_%d);\n', o(2), o(3)); 
        %     end
        %     idx=idx+1;
        % 
        % end
    end
end



for Colum_indx=K:-1:1
    fprintf(fileID, ' x_next(%d)<= ',K-Colum_indx);
    n_not_empty = nnz(~cellfun(@isempty, M(:, Colum_indx)));
    for Row_indx=(1:n_not_empty)
        o = M{Row_indx,Colum_indx};
        if Row_indx==1
                if isempty(M{Row_indx+1,Colum_indx}) %|| Row_indx+1<R
                    fprintf(fileID,'((s_%d_%d));\n', o(1), o(2));                 
                else 
                    fprintf(fileID,'((s_%d_%d) xor', o(1), o(2));
                end
            elseif Row_indx+1==R+1
                fprintf(fileID,' (s_%d_%d));\n', o(1), o(2)); 
            elseif Row_indx>1 && ((isempty(M{Row_indx+1,Colum_indx}))) %|| (Row_indx+1==R+1))
                fprintf(fileID,' (s_%d_%d));\n', o(1), o(2));
            elseif Row_indx>1 && ((~isempty(M{Row_indx+1,Colum_indx}))|| (Row_indx+1<=R))  
                fprintf(fileID,' (s_%d_%d) xor', o(1), o(2)); 
            elseif Row_indx>1 && ((~isempty(M{Row_indx+1,Colum_indx})) && (Row_indx+1>R+1))  
                fprintf(fileID,' (s_%d_%d);\n', o(1), o(2)); 
       end
    end
end


fprintf(fileID, '  process(clk)\n');
fprintf(fileID, '  begin\n');
fprintf(fileID, '    if rising_edge(clk) then\n');
fprintf(fileID, '      if rst = ''1'' then\n');
fprintf(fileID, '        x_reg <= (others => ''0'');\n');
fprintf(fileID, '      elsif load = ''1'' then\n');
fprintf(fileID, '        x_reg <= a;\n');
fprintf(fileID, '      elsif en = ''1'' then\n');
fprintf(fileID, '        x_reg <= x_next;\n');
fprintf(fileID, '      end if;\n');
fprintf(fileID, '    end if;\n');
fprintf(fileID, '  end process;\n\n');
fprintf(fileID, '  s <= x_reg;\n');
fprintf(fileID, 'end rtl;\n');

fclose(fileID);