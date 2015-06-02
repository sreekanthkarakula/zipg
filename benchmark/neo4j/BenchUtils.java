import org.neo4j.graphdb.GraphDatabaseService;
import org.neo4j.graphdb.schema.Schema;

import java.util.concurrent.TimeUnit;

public class BenchUtils {

    public static void registerShutdownHook(final GraphDatabaseService graphDb) {
        Runtime.getRuntime().addShutdownHook(new Thread() {
            public void run() {
                graphDb.shutdown();
            }
        });
    }

    public static void awaitIndexes(GraphDatabaseService graphDb) {
        Schema schema = graphDb.schema();
        try {
            schema.awaitIndexesOnline(60, TimeUnit.SECONDS);
        } catch (IllegalStateException e) {
            System.err.println("indexes not all online after 60 seconds: " + e.getMessage());
            throw e;
        }
    }

}
