/* $Id$ */

#ifndef __DIVA_PCI_INTERFACE_H__
#define __DIVA_PCI_INTERFACE_H__

void*  divasa_remap_pci_bar (unsigned long  bar, unsigned long area_length);
void  divasa_unmap_pci_bar (void* bar);
unsigned long divasa_get_pci_irq (unsigned char bus, unsigned char func, void* pci_dev_handle);
unsigned long divasa_get_pci_bar (unsigned char bus, unsigned char func, int bar, void* pci_dev_handle);
typedef void (*divasa_find_pci_proc_t)(int handle, unsigned char bus, unsigned char func, void* pci_dev_handle);
void divasa_find_card_by_type (unsigned short device_id, divasa_find_pci_proc_t signal_card_fn, int handle);

#endif
