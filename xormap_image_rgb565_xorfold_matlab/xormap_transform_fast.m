function result = xormap_transform_fast(input, plan)
%XORMAP_TRANSFORM_FAST Prefix-XOR equivalent of XORMAP_TRANSFORM.
%   RESULT = XORMAP_TRANSFORM_FAST(INPUT, PLAN), where PLAN comes from
%   XORMAP_FAST_PLAN, produces exactly the same K-bit next state as
%   xormap_transform(input, K).  INPUT and RESULT are LSB-first logical
%   row vectors.
%
%   xormap_transform XORs symmetric pairs in a contiguous window.  The
%   parity of an even-size window is its prefix-XOR range.  For an odd-size
%   window the unpaired centre bit is omitted, so it is XORed back out.

    if ~isstruct(plan) || ~isfield(plan, 'k')
        error('xormap_transform_fast:badPlan', 'plan must come from xormap_fast_plan');
    end
    if ~isvector(input) || numel(input) ~= plan.k
        error('xormap_transform_fast:invalidInput', ...
            'input must be a 1x%d bit vector', plan.k);
    end
    if ~islogical(input) && any(input(:) ~= 0 & input(:) ~= 1)
        error('xormap_transform_fast:invalidInput', 'input values must be binary');
    end

    input = logical(input(:)).';
    prefix = [false, mod(cumsum(input), 2) ~= 0];
    result = xor(prefix(plan.right + 1), prefix(plan.left));
    result(plan.odd_positions) = xor( ...
        result(plan.odd_positions), input(plan.centers));
end
