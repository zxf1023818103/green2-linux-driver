package cn.zenghome.flashstudio;

import android.os.ParcelFileDescriptor;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructTimeval;
import android.util.Log;
import java.io.*;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.Set;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Wire-compatible NAN-PING and NAN-THROUGHPUT/1 server, scoped to an Aware network. */
final class ProbeServer implements AutoCloseable {
    private final ParcelFileDescriptor listener;
    private final AtomicBoolean closed = new AtomicBoolean();
    private final Set<ParcelFileDescriptor> clients = ConcurrentHashMap.newKeySet();
    private final ThreadPoolExecutor workers = new ThreadPoolExecutor(0, 8, 30, TimeUnit.SECONDS,
            new SynchronousQueue<>());
    private final ScheduledExecutorService watchdog = Executors.newSingleThreadScheduledExecutor();
    ProbeServer(long network, String address, int scope, int port) throws IOException {
        int fd = NativeTransport.listen(network, address, scope, port);
        if (fd < 0) throw new IOException("listen errno=" + -fd);
        listener = ParcelFileDescriptor.adoptFd(fd);
        Thread acceptor = new Thread(this::accept, "aware-probe-accept");
        acceptor.start();
    }
    private void accept() {
        while (!closed.get()) {
            ParcelFileDescriptor client = null;
            try {
                int fd = NativeTransport.accept(listener.getFd());
                if (fd < 0) throw new IOException("accept errno=" + -fd);
                client = ParcelFileDescriptor.adoptFd(fd);
                clients.add(client);
                ParcelFileDescriptor accepted = client;
                workers.execute(() -> handle(accepted));
            } catch (Exception e) {
                if (client != null) { clients.remove(client); quietly(client); }
                if (!closed.get()) Log.w("Green2Services", "accept: " + e.getClass().getSimpleName());
            }
        }
    }
    private static void quietly(AutoCloseable c) { try { c.close(); } catch (Exception ignored) {} }
    private static void closeSocket(ParcelFileDescriptor fd) {
        try { Os.shutdown(fd.getFileDescriptor(), OsConstants.SHUT_RDWR); } catch (Exception ignored) {}
        quietly(fd);
    }
    private static byte[] exact(DataInputStream in, int n) throws IOException {
        byte[] b = new byte[n]; in.readFully(b); return b;
    }
    private static String line(DataInputStream in) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        for (int i = 0; i < 256; i++) { int v=in.read(); if(v<0)throw new EOFException();
            if(v==10)return b.toString(StandardCharsets.US_ASCII.name()); b.write(v); }
        throw new IOException("command too long");
    }
    private void handle(ParcelFileDescriptor client) {
        ScheduledFuture<?> timer = null;
        try (InputStream rawIn = new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.dup(client.getFileDescriptor()));
             OutputStream rawOut = new ParcelFileDescriptor.AutoCloseOutputStream(ParcelFileDescriptor.dup(client.getFileDescriptor()))) {
            Os.setsockoptTimeval(client.getFileDescriptor(), OsConstants.SOL_SOCKET, OsConstants.SO_RCVTIMEO, StructTimeval.fromMillis(10000));
            Os.setsockoptTimeval(client.getFileDescriptor(), OsConstants.SOL_SOCKET, OsConstants.SO_SNDTIMEO, StructTimeval.fromMillis(10000));
            DataInputStream in=new DataInputStream(new BufferedInputStream(rawIn,65536));
            DataOutputStream out=new DataOutputStream(new BufferedOutputStream(rawOut,65536));
            while (!closed.get()) {
                timer=watchdog.schedule(()->closeSocket(client),10,TimeUnit.SECONDS);
                String command=line(in);
                if(command.equals("NAN-PING")){out.writeBytes("NAN-PONG\n");out.flush();return;}
                String[] f=command.trim().split("\\s+");
                if(f.length!=4||!f[0].equals("NAN-THROUGHPUT/1")||!f[1].matches("[A-Za-z0-9-]{1,64}"))throw new IOException("bad command");
                int seconds=Integer.parseInt(f[3]);boolean upload=f[2].equals("UPLOAD");
                if((!upload&&!f[2].equals("DOWNLOAD"))||seconds<1||seconds>60)throw new IOException("bad duration or direction");
                // A socket-level send timeout also bounds silent download clients.
                timer.cancel(false);timer=watchdog.schedule(()->closeSocket(client),seconds+30,TimeUnit.SECONDS);
                out.writeBytes("READY\n");out.flush();long start=System.nanoTime(),bytes=0;
                if(upload){while(true){int n=in.readInt();if(n==0)break;if(n<0||n>262144)throw new IOException("bad frame");exact(in,n);bytes+=n;}}
                else{byte[] block=new byte[65536];for(int i=0;i<block.length;i++)block[i]=(byte)i;
                    while(System.nanoTime()-start<seconds*1000000000L){out.writeInt(block.length);out.write(block);out.flush();bytes+=block.length;}
                    out.writeInt(0);out.flush();}
                double elapsed=(System.nanoTime()-start)/1e9;
                out.writeBytes("{\"version\":1,\"test_id\":\""+f[1]+"\",\"direction\":\""+(upload?"upload":"download")+"\",\"bytes\":"+bytes+",\"seconds\":"+elapsed+",\"mbps\":"+(bytes*8/elapsed/1e6)+",\"counter\":\""+(upload?"received":"submitted")+"\"}\n");out.flush();timer.cancel(false);timer=null;
            }
        } catch (Exception e) { if(!closed.get())Log.i("Green2Services","probe client ended: "+e.getClass().getSimpleName()); }
        finally { if(timer!=null)timer.cancel(false);clients.remove(client);quietly(client); }
    }
    @Override public void close() { if(closed.compareAndSet(false,true)){closeSocket(listener);for(ParcelFileDescriptor c:clients)closeSocket(c);workers.shutdownNow();watchdog.shutdownNow();} }
}
