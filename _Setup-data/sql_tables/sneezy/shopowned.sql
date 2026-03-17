/*M!999999\- enable the sandbox mode */ 

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*M!100616 SET @OLD_NOTE_VERBOSITY=@@NOTE_VERBOSITY, NOTE_VERBOSITY=0 */;
DROP TABLE IF EXISTS `shopowned`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8mb4 */;
CREATE TABLE `shopowned` (
  `shop_nr` int(11) NOT NULL DEFAULT 0,
  `profit_buy` double NOT NULL DEFAULT 0,
  `profit_sell` double NOT NULL DEFAULT 0,
  `max_num` int(11) DEFAULT NULL,
  `corp_id` bigint(20) unsigned DEFAULT NULL,
  `dividend` double DEFAULT NULL,
  `reserve_max` int(11) DEFAULT NULL,
  `reserve_min` int(11) DEFAULT NULL,
  `no_such_item1` varchar(127) DEFAULT NULL,
  `no_such_item2` varchar(127) DEFAULT NULL,
  `do_not_buy` varchar(127) DEFAULT NULL,
  `missing_cash1` varchar(127) DEFAULT NULL,
  `missing_cash2` varchar(127) DEFAULT NULL,
  `message_buy` varchar(127) DEFAULT NULL,
  `message_sell` varchar(127) DEFAULT NULL,
  `tax_nr` int(11) DEFAULT NULL,
  `gold` int(11) DEFAULT NULL,
  PRIMARY KEY (`shop_nr`),
  KEY `idx_shopowned_corp_id` (`corp_id`),
  KEY `idx_shopowned_tax_nr` (`tax_nr`),
  CONSTRAINT `fk_shopowned_corp_id` FOREIGN KEY (`corp_id`) REFERENCES `corporation` (`corp_id`) ON DELETE SET NULL,
  CONSTRAINT `fk_shopowned_shop_nr` FOREIGN KEY (`shop_nr`) REFERENCES `shop` (`shop_nr`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*M!100616 SET NOTE_VERBOSITY=@OLD_NOTE_VERBOSITY */;

