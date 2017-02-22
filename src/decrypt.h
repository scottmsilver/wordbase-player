#include <vector>

#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/evp.h>

void handleErrors(void)
{
  ERR_print_errors_fp(stderr);
  abort();
}

std::string sha1(const std::string &input)
{
  BIO* p_bio_md  = nullptr;
  BIO* p_bio_mem = nullptr;
  
  try {
    // make chain: p_bio_md <-> p_bio_mem
    p_bio_md = BIO_new(BIO_f_md());
    if (!p_bio_md) throw std::bad_alloc();
    BIO_set_md(p_bio_md, EVP_sha1());
    
    p_bio_mem = BIO_new_mem_buf((void*)input.c_str(), input.length());
    if (!p_bio_mem) throw std::bad_alloc();
    BIO_push(p_bio_md, p_bio_mem);
    
    // read through p_bio_md
    // read sequence: buf <<-- p_bio_md <<-- p_bio_mem
    std::vector<char> buf(input.size());
    for (;;)
    {
      auto nread = BIO_read(p_bio_md, buf.data(), buf.size());
      if (nread  < 0) { throw std::runtime_error("BIO_read failed"); }
      if (nread == 0) { break; } // eof
    }
    
    // get result
    char md_buf[EVP_MAX_MD_SIZE];
    auto md_len = BIO_gets(p_bio_md, md_buf, sizeof(md_buf));
    if (md_len <= 0) { throw std::runtime_error("BIO_gets failed"); }
    
    std::string result(md_buf, md_len);
    
    // clean
    BIO_free_all(p_bio_md);
    
    return result;
  } catch (...) {
    if (p_bio_md) { BIO_free_all(p_bio_md); }
    throw;
  }
}

int decrypt(unsigned char *ciphertext, int ciphertext_len, unsigned char *key,
            unsigned char *iv, unsigned char *plaintext)
{
  EVP_CIPHER_CTX *ctx;
  
  int len;
  
  int plaintext_len;
  
  /* Create and initialise the context */
  if(!(ctx = EVP_CIPHER_CTX_new())) handleErrors();
  
  /* Initialise the decryption operation. IMPORTANT - ensure you use a key
   * and IV size appropriate for your cipher
   * In this example we are using 256 bit AES (i.e. a 256 bit key). The
   * IV size for *most* modes is the same as the block size. For AES this
   * is 128 bits */
  
  if(1 != EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, iv))
    handleErrors();
  
  if (1 != EVP_CIPHER_CTX_set_padding(ctx, 0)) handleErrors();
  
  /* Provide the message to be decrypted, and obtain the plaintext output.
   * EVP_DecryptUpdate can be called multiple times if necessary
   */
  if(1 != EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len))
    handleErrors();
  plaintext_len = len;
  
  /* Finalise the decryption. Further plaintext bytes may be written at
   * this stage.
   */
  if(1 != EVP_DecryptFinal_ex(ctx, plaintext + len, &len)) handleErrors();
  plaintext_len += len;
  
  /* Clean up */
  EVP_CIPHER_CTX_free(ctx);
  
  return plaintext_len;
}
