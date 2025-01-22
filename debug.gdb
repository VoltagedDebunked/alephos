target remote :1234

b vmm_init
b vmm.c:85
c
p hhdm_request
p &hddm_request
p/x hhdm_request.response
x/10x hhdm_request.response
info mem
p kernel_address_request
p/x kernel_address_request.response
n
p hddm_request.response->offset
info registers
bt
x/20x $rsp
x/20x $rbp
info variables hhdm_request
info variables kernel_address_request
maintenance info sections
info breakpoints
p/x hhdm_request.id
p/x LIMINE_HHDM_REQUEST