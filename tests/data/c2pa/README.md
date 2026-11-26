# C2PA test signing certificate

`es256.pub` (certificate chain) and `es256.pem` (its private key) are the
public **test** credentials from the C2PA Rust SDK, copied from
[`contentauth/c2pa-rs`](https://github.com/contentauth/c2pa-rs) at tag
`c2pa-v0.91.9`, path `sdk/tests/fixtures/certs/` (MIT / Apache-2.0).

They are published for testing only: the key is public, and the certificate
(`O = C2PA Test Signing Cert, OU = FOR TESTING_ONLY`) is not on the C2PA trust
list. `content_credentials_test` signs with them. You can also point Lumen's
Content Credentials dialog at them to try the feature. **Never use them for real
images:** anyone can sign with this key.
