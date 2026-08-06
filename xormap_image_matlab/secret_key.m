function bits = secret_key(k)
%SECRET_KEY Fixed, reproducible "external" secret key bits for this demo
%   pipeline. In a real deployment this would be a pre-shared secret kept
%   off the image entirely; here it's a fixed PRNG pattern so every run
%   (encrypt and decrypt) agrees without passing a key around by hand.
%   Restores the caller's RNG state afterwards so it doesn't disturb
%   unrelated random sampling (e.g. adjacent_correlation).

    prev = rng;
    rng(1729);
    bits = rand(1, k) > 0.5;
    rng(prev);
end
