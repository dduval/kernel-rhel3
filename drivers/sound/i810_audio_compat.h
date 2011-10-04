#ifndef __I810_AUDIO_COMPAT_H__
#define __I810_AUDIO_COMPAT_H__

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

#define __user

#define iminor(x) MINOR((x)->i_rdev)

#endif /* __I810_AUDIO_COMPAT_H__ */
