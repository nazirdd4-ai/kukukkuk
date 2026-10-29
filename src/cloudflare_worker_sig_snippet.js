// cloudflare_worker_sig_snippet.js — paste into the existing worker. Not a full worker,
// just the piece that turns {valid} into {valid, sig}. Set SIG_KEY as a Worker secret:
//   wrangler secret put SIG_KEY
// with the EXACT same 64-hex-char value as the repo's SIG_KEY GitHub Actions secret —
// see .github/workflows/build_obus.yml header for how that one's generated.

async function signLicenseResult(licenseKey, valid, env) {
  const sigKeyBytes = hexToBytes(env.SIG_KEY); // 32 raw bytes
  const msg = new TextEncoder().encode(`${licenseKey}|${valid ? '1' : '0'}`);
  const cryptoKey = await crypto.subtle.importKey(
    'raw', sigKeyBytes, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']
  );
  const sigBuf = await crypto.subtle.sign('HMAC', cryptoKey, msg);
  return bytesToHex(new Uint8Array(sigBuf));
}

function hexToBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
  return out;
}
function bytesToHex(bytes) {
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
}

// Wherever the worker currently does:
//   return new Response(JSON.stringify({ valid: true }), ...);
// change to:
//   const sig = await signLicenseResult(licenseKey, true, env);
//   return new Response(JSON.stringify({ valid: true, sig }), ...);
//
// licenseKey here must be the exact string the client sent in {"licenseKey":"<KEY>"} — the
// client signs/verifies against that literal value, not a normalized or re-cased copy.
// Rejections ({valid:false}) don't need a sig — the client only checks it when valid:true,
// since a forged *rejection* gains an attacker nothing.
