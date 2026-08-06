function [chunk1]=demo_chunks(k)
% DEMO_CHUNKS  Generate chunk1/chunk2/chunk3 like the Python code and print them.
%
% Usage:
%   demo_chunks(8)
    chunk1 = chunk_to_cell_of_triples(generate_chunk1_arrays(k));
end
% -------------------------------------------------------------------------
% Chunk 1 (d = 1)
% -------------------------------------------------------------------------
function chunk1 = generate_chunk1_arrays(k)
    C = 2*k - 2;
    chunk1 = cell(1, C);
    % Python: chunk1_array[0] = []
    if C >= 1
        chunk1{1} = [];
    end
    % Python: for cl in range(1, C):
    for cl = 1:(C-1)
        col = zeros(0,3,'uint32');
        if cl <= (k-1)
            base_i = k - cl;
            base_j = k;
        else
            base_i = 1;
            base_j = 2*k - 1 - cl;
        end
        for n = 0:(k-1)
            i = base_i + n;
            j = base_j - n;
            if i >= j
                break;
            else
                col(end+1,:) = uint32([1, i, j]); %#ok<AGROW>
            end
        end
        chunk1{cl+1} = col;  % MATLAB index cl+1 corresponds to Python cl
    end
end
