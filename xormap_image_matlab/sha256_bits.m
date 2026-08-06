function bits = sha256_bits(byte_vector)
%SHA256_BITS SHA-256 digest of a byte vector, as a 1x256 logical vector.
%   Uses Java's MessageDigest (bundled with MATLAB, no toolbox needed).
%   Java's byte[] is signed, so digest values are unwrapped to 0-255
%   before expanding to bits via BYTES_TO_BITS.

    md = java.security.MessageDigest.getInstance('SHA-256');
    digest = md.digest(uint8(byte_vector(:)));
    digest_bytes = uint8(mod(double(digest) + 256, 256));
    bits = bytes_to_bits(digest_bytes(:).');
end
